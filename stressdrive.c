// stressdrive.c 1.4
//   Copyright (c) 2011-2023 Jonathan 'Wolf' Rentzsch: http://rentzsch.com
//   Some rights reserved: http://opensource.org/licenses/mit
//   https://github.com/rentzsch/stressdrive

#define _BSD_SOURCE

#include <fcntl.h>
#include <inttypes.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __APPLE__
#import <IOKit/pwr_mgt/IOPMLib.h>
#include <sys/disk.h>
#endif

#ifdef __linux__
#include <linux/fs.h>
#endif

#define HASH_DIGEST_LENGTH SHA_DIGEST_LENGTH
#define HASH_INIT_FUNCTION EVP_sha1

#define CIPHER_KEY_SIZE (128 / 8)
#define CIPHER_BLOCK_SIZE AES_BLOCK_SIZE
#define CIPHER_INIT_FUNCTION EVP_aes_128_ctr

#define EXIT_CALL_FAILED 2

#define MAX(a, b)                                                              \
    ({                                                                         \
        __typeof__(a) _a = (a);                                                \
        __typeof__(b) _b = (b);                                                \
        _a > _b ? _a : _b;                                                     \
    })

#define MIN(a, b)                                                              \
    ({                                                                         \
        __typeof__(a) _a = (a);                                                \
        __typeof__(b) _b = (b);                                                \
        _a < _b ? _a : _b;                                                     \
    })

#define KILO 1000
#define MEGA 1000000
#define GIGA 1000000000

typedef struct {
    uint64_t total;
    const char *name;
    struct timeval start, last_display;
} PROGRESS_CTX;

void PROGRESS_Init(PROGRESS_CTX *ctx, uint64_t total, const char *name) {
    ctx->total = total;
    ctx->name = name;
    gettimeofday(&ctx->start, NULL);
    ctx->last_display = (struct timeval){0};
}

void _PROGRESS_Print(PROGRESS_CTX *ctx, struct timeval *now, uint64_t current,
                     uint32_t blockSize) {
    double complete = (double)current / (double)ctx->total;
    printf("\r%s %.1f%% (%" PRIu64 " of %" PRIu64 ")", ctx->name,
           complete * 100.0, current, ctx->total);

    uint64_t elapsed = now->tv_sec - ctx->start.tv_sec;
    printf(" %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 "", elapsed / 3600,
           (elapsed / 60) % 60, elapsed % 60);

    if (elapsed > 0) {
        double speed = (double)current * blockSize / elapsed;

        if (speed > GIGA) {
            printf(" (%.1f GB/s)", speed / GIGA);
        } else if (speed > MEGA) {
            printf(" (%.1f MB/s)", speed / MEGA);
        } else if (speed > KILO) {
            printf(" (%.1f KB/s)", speed / KILO);
        } else {
            printf(" (%.1f B/s)", speed);
        }
    }

    if (current != ctx->total && elapsed > 10 && complete > 0.001) {
        uint64_t eta = (1 / complete - 1) * elapsed;
        printf(" (ETA: %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ")", eta / 3600,
               (eta / 60) % 60, eta % 60);
    }

    printf("\e[K");
    fflush(stdout);
}

void PROGRESS_Update(PROGRESS_CTX *ctx, uint64_t current, uint32_t blockSize) {
    struct timeval now, delta;
    gettimeofday(&now, NULL);
    timersub(&now, &ctx->last_display, &delta);
    if (delta.tv_sec < 1)
        return;
    ctx->last_display = now;
    _PROGRESS_Print(ctx, &now, current, blockSize);
}

void PROGRESS_Finish(PROGRESS_CTX *ctx, uint32_t blockSize) {
    struct timeval now;
    gettimeofday(&now, NULL);
    _PROGRESS_Print(ctx, &now, ctx->total, blockSize);
    printf("\n");
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy
#endif

void DIGEST_Init(EVP_MD_CTX *digestContext) {
    if (1 != EVP_DigestInit_ex(digestContext, HASH_INIT_FUNCTION(), NULL)) {
        fprintf(stderr, "Digest initialisation failed\n");
        exit(EXIT_CALL_FAILED);
    }
}

void DIGEST_Update(EVP_MD_CTX *digestContext, const void *d, size_t cnt) {
    if (1 != EVP_DigestUpdate(digestContext, d, cnt)) {
        fprintf(stderr, "Digest update failed\n");
        exit(EXIT_CALL_FAILED);
    }
}

void DIGEST_Final(EVP_MD_CTX *digestContext, unsigned char *digest) {
    if (1 != EVP_DigestFinal_ex(digestContext, digest, NULL)) {
        fprintf(stderr, "Digest finalisation failed\n");
        exit(EXIT_CALL_FAILED);
    }
}

void DIGEST_Print(unsigned char *digest, const char *name) {
    for (size_t i = 0; i < HASH_DIGEST_LENGTH; i++) {
        printf("%02x", digest[i]);
    }
    printf(" <= hash digest of %s data\n", name);
}

#define BUFFER_COUNT 2

typedef enum {
    Generate,
    Process,
    Read,
    Hash,
} Action;

typedef struct {
    uint8_t *data;
    Action action;
    bool written, hashed;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Buffer;

typedef struct {
    Buffer buffers[BUFFER_COUNT];
    uint64_t blockCount;
    uint16_t bufferBlocks;
    uint32_t blockSize;
    PROGRESS_CTX *progress;
    EVP_CIPHER_CTX *cipher;
    unsigned char *cipherInput;
    int fd;
} Shared;

void *generator_thread(void *arg) {
    Shared *shared = (Shared *)arg;

    uint64_t blockCount = shared->blockCount;
    uint16_t bufferBlocks = shared->bufferBlocks;
    uint32_t blockSize = shared->blockSize;
    PROGRESS_CTX *progress = shared->progress;
    EVP_CIPHER_CTX *cipher = shared->cipher;
    unsigned char *cipherInput = shared->cipherInput;

    int bufferIndex = 0;
    Buffer *buffer = &shared->buffers[0];
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        if (blockIndex)
            PROGRESS_Update(progress, blockIndex, blockSize);

        uint32_t size =
            (uint32_t)MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        if (buffer->action != Generate) {
            pthread_mutex_lock(&buffer->mutex);
            while (buffer->action != Generate) {
                pthread_cond_wait(&buffer->cond, &buffer->mutex);
            }
            pthread_mutex_unlock(&buffer->mutex);
        }

        int outSize;
        if (!EVP_EncryptUpdate(cipher, buffer->data, &outSize, cipherInput,
                               size)) {
            fprintf(stderr, "EVP_EncryptUpdate() failed\n");
            exit(EXIT_CALL_FAILED);
        }
        if (outSize != size) {
            fprintf(stderr,
                    "EVP_EncryptUpdate() returned %d instead of %u bytes\n",
                    outSize, size);
            exit(EXIT_CALL_FAILED);
        }

        pthread_mutex_lock(&buffer->mutex);
        buffer->action = Process;
        pthread_cond_broadcast(&buffer->cond);
        pthread_mutex_unlock(&buffer->mutex);

        bufferIndex = (bufferIndex + 1) % BUFFER_COUNT;
        buffer = &shared->buffers[bufferIndex];
    }

    return NULL;
}

void *writer_thread(void *arg) {
    Shared *shared = (Shared *)arg;

    uint64_t blockCount = shared->blockCount;
    uint16_t bufferBlocks = shared->bufferBlocks;
    uint32_t blockSize = shared->blockSize;
    int fd = shared->fd;

    int bufferIndex = 0;
    Buffer *buffer = &shared->buffers[0];
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        uint32_t size =
            (uint32_t)MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        if (buffer->action != Process || buffer->written) {
            pthread_mutex_lock(&buffer->mutex);
            while (buffer->action != Process || buffer->written) {
                pthread_cond_wait(&buffer->cond, &buffer->mutex);
            }
            pthread_mutex_unlock(&buffer->mutex);
        }

        if (write(fd, buffer->data, size) != size) {
            perror("write() failed");
            exit(EXIT_CALL_FAILED);
        }

        pthread_mutex_lock(&buffer->mutex);
        if (buffer->hashed) {
            buffer->action = Generate;
            buffer->hashed = false;
        } else {
            buffer->written = true;
        }
        pthread_cond_broadcast(&buffer->cond);
        pthread_mutex_unlock(&buffer->mutex);

        bufferIndex = (bufferIndex + 1) % BUFFER_COUNT;
        buffer = &shared->buffers[bufferIndex];
    }

    return NULL;
}

void *reader_thread(void *arg) {
    Shared *shared = (Shared *)arg;

    uint64_t blockCount = shared->blockCount;
    uint16_t bufferBlocks = shared->bufferBlocks;
    uint32_t blockSize = shared->blockSize;
    PROGRESS_CTX *progress = shared->progress;
    int fd = shared->fd;

    int bufferIndex = 0;
    Buffer *buffer = &shared->buffers[0];
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        if (blockIndex)
            PROGRESS_Update(progress, blockIndex, blockSize);

        uint32_t size =
            (uint32_t)MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        if (buffer->action != Read) {
            pthread_mutex_lock(&buffer->mutex);
            while (buffer->action != Read) {
                pthread_cond_wait(&buffer->cond, &buffer->mutex);
            }
            pthread_mutex_unlock(&buffer->mutex);
        }

        if (read(fd, buffer->data, size) == -1) {
            perror("read() failed");
            exit(EXIT_CALL_FAILED);
        }

        pthread_mutex_lock(&buffer->mutex);
        buffer->action = Hash;
        pthread_cond_broadcast(&buffer->cond);
        pthread_mutex_unlock(&buffer->mutex);

        bufferIndex = (bufferIndex + 1) % BUFFER_COUNT;
        buffer = &shared->buffers[bufferIndex];
    }

    return NULL;
}

int main(int argc, const char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "stressdrive v1.4\n");
#ifdef __APPLE__
        fprintf(stderr, "Usage: sudo %s /dev/rdiskN\n", argv[0]);
#else
        fprintf(stderr, "Usage: sudo %s /dev/sdX\n", argv[0]);
#endif
        exit(EXIT_FAILURE);
    }

    const char *drivePath = argv[1];
    int fd = open(drivePath, O_RDWR);
    if (fd == -1) {
        perror("open() failed");
        exit(EXIT_CALL_FAILED);
    }

    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        perror("flock() failed");
        exit(EXIT_CALL_FAILED);
    }

    uint32_t blockSize;
#ifdef DKIOCGETBLOCKSIZE
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) == -1) {
#else
    if (ioctl(fd, BLKSSZGET, &blockSize) == -1) {
#endif
        perror("getting block size using ioctl failed");
        exit(EXIT_CALL_FAILED);
    }
    printf("disk block size: %u\n", blockSize);

    uint64_t blockCount;
#ifdef DKIOCGETBLOCKCOUNT
    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) == -1) {
#else
    if (ioctl(fd, BLKGETSIZE64, &blockCount) != -1) {
        blockCount /= blockSize;
    } else {
#endif
        perror("getting block count using ioctl failed");
        exit(EXIT_CALL_FAILED);
    }
    printf("disk block count: %" PRIu64 "\n", blockCount);

    uint32_t bufferSize = MAX(blockSize, 8 * 1024 * 1024);
    printf("buffer size: %u\n", bufferSize);

    uint16_t bufferBlocks = bufferSize / blockSize;
    uint32_t checkFrequency = 1024 * 1024 * 1024 / blockSize;
    uint64_t checkCount = (blockCount + bufferBlocks - 1) / checkFrequency;
    uint8_t *checkDigests = malloc(checkCount * HASH_DIGEST_LENGTH);
    if (checkDigests == NULL) {
        perror("malloc() failed");
        exit(EXIT_CALL_FAILED);
    }

#ifdef __APPLE__
    IOPMAssertionID noIdleSleepAssertionID;
    IOReturn noIdleSleepAssertionCreated = IOPMAssertionCreateWithName(
        kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn,
        CFSTR("stressdrive running"), &noIdleSleepAssertionID);
    if (kIOReturnSuccess == noIdleSleepAssertionCreated) {
        printf("succesfully created no idle sleep assertion\n");
    } else {
        printf("failed to create no idle sleep assertion\n");
    }
#endif

    EVP_MD_CTX *digestContext;
    if ((digestContext = EVP_MD_CTX_new()) == NULL) {
        fprintf(stderr, "Digest context creation failed\n");
        exit(EXIT_CALL_FAILED);
    }

    PROGRESS_CTX progress;

    unsigned char cipherKey[CIPHER_KEY_SIZE];
    if (!RAND_bytes(cipherKey, CIPHER_KEY_SIZE)) {
        fprintf(stderr, "RAND_bytes() failed\n");
        exit(EXIT_CALL_FAILED);
    }

    unsigned char cipherIv[CIPHER_BLOCK_SIZE];
    if (!RAND_bytes(cipherIv, CIPHER_BLOCK_SIZE)) {
        fprintf(stderr, "RAND_bytes() failed\n");
        exit(EXIT_CALL_FAILED);
    }

    EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
    if (!cipher) {
        fprintf(stderr, "EVP_CIPHER_CTX_new() failed\n");
        exit(EXIT_CALL_FAILED);
    }
    if (!EVP_EncryptInit(cipher, CIPHER_INIT_FUNCTION(), cipherKey, cipherIv)) {
        fprintf(stderr, "EVP_EncryptInit() failed\n");
        exit(EXIT_CALL_FAILED);
    }

    unsigned char *cipherInput = malloc(bufferSize);
    memset(cipherInput, 0, bufferSize);

    Shared *shared = &(Shared){
        .blockCount = blockCount,
        .bufferBlocks = bufferBlocks,
        .blockSize = blockSize,
        .progress = &progress,
        .cipher = cipher,
        .cipherInput = cipherInput,
        .fd = fd,
    };

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if ((shared->buffers[i].data = malloc(bufferSize)) == NULL) {
            perror("malloc() failed");
            exit(EXIT_CALL_FAILED);
        }
        shared->buffers[i].action = Generate;
        if (pthread_mutex_init(&shared->buffers[i].mutex, NULL) != 0) {
            perror("pthread_mutex_init() failed");
            exit(EXIT_CALL_FAILED);
        }
        if (pthread_cond_init(&shared->buffers[i].cond, NULL) != 0) {
            perror("pthread_cond_init() failed");
            exit(EXIT_CALL_FAILED);
        }
    }

    pthread_t t_reader, t_generator, t_writer;
    int bufferIndex;
    Buffer *buffer;

    printf("writing random data to %s\n", drivePath);
    DIGEST_Init(digestContext);
    PROGRESS_Init(&progress, blockCount, "writing");

    pthread_create(&t_generator, NULL, generator_thread, shared);
    pthread_create(&t_writer, NULL, writer_thread, shared);

    bufferIndex = 0;
    buffer = &shared->buffers[0];
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        uint32_t size =
            (uint32_t)MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        if (buffer->action != Process || buffer->hashed) {
            pthread_mutex_lock(&buffer->mutex);
            while (buffer->action != Process || buffer->hashed) {
                pthread_cond_wait(&buffer->cond, &buffer->mutex);
            }
            pthread_mutex_unlock(&buffer->mutex);
        }

        DIGEST_Update(digestContext, buffer->data, size);

        pthread_mutex_lock(&buffer->mutex);
        if (buffer->written) {
            buffer->action = Generate;
            buffer->written = false;
        } else {
            buffer->hashed = true;
        }
        pthread_cond_broadcast(&buffer->cond);
        pthread_mutex_unlock(&buffer->mutex);

        bufferIndex = (bufferIndex + 1) % BUFFER_COUNT;
        buffer = &shared->buffers[bufferIndex];

        if ((blockIndex + bufferBlocks) % checkFrequency == 0) {
            uint64_t checkIndex = blockIndex / checkFrequency;
            DIGEST_Final(digestContext,
                         checkDigests + checkIndex * HASH_DIGEST_LENGTH);
            DIGEST_Init(digestContext);
        }
    }

    pthread_join(t_writer, NULL);
    pthread_join(t_generator, NULL);

    PROGRESS_Finish(&progress, blockSize);
    EVP_CIPHER_CTX_free(cipher);

    uint8_t writtenHashDigest[HASH_DIGEST_LENGTH];
    DIGEST_Final(digestContext, writtenHashDigest);
    DIGEST_Print(writtenHashDigest, "written");

    if (lseek(fd, 0LL, SEEK_SET) != 0LL) {
        perror("lseek() failed");
        exit(EXIT_CALL_FAILED);
    }

    int exitCode = EXIT_SUCCESS;
    uint8_t readHashDigest[HASH_DIGEST_LENGTH];

    for (int i = 0; i < BUFFER_COUNT; i++) {
        shared->buffers[i].action = Read;
    }

    printf("verifying written data\n");
    DIGEST_Init(digestContext);
    PROGRESS_Init(&progress, blockCount, "reading");

    pthread_create(&t_reader, NULL, reader_thread, shared);

    bufferIndex = 0;
    buffer = &shared->buffers[0];
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        uint32_t size =
            (uint32_t)MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        if (buffer->action != Hash) {
            pthread_mutex_lock(&buffer->mutex);
            while (buffer->action != Hash) {
                pthread_cond_wait(&buffer->cond, &buffer->mutex);
            }
            pthread_mutex_unlock(&buffer->mutex);
        }

        DIGEST_Update(digestContext, buffer->data, size);

        pthread_mutex_lock(&buffer->mutex);
        buffer->action = Read;
        pthread_cond_broadcast(&buffer->cond);
        pthread_mutex_unlock(&buffer->mutex);

        bufferIndex = (bufferIndex + 1) % BUFFER_COUNT;
        buffer = &shared->buffers[bufferIndex];

        if ((blockIndex + bufferBlocks) % checkFrequency == 0) {
            uint64_t checkIndex = blockIndex / checkFrequency;
            DIGEST_Final(digestContext, readHashDigest);
            DIGEST_Init(digestContext);
            if (bcmp(checkDigests + checkIndex * HASH_DIGEST_LENGTH,
                     readHashDigest, HASH_DIGEST_LENGTH) != 0) {
                printf("\nFailed intermediate checksum for bytes %" PRIu64
                       "...%" PRIu64 "\n",
                       (blockIndex + bufferBlocks - checkFrequency) * blockSize,
                       blockIndex * blockSize + size);
                exitCode = EXIT_FAILURE;
            }
        }
    }

    pthread_join(t_reader, NULL);

    PROGRESS_Finish(&progress, blockSize);
    DIGEST_Final(digestContext, readHashDigest);
    DIGEST_Print(readHashDigest, "read");
    EVP_MD_CTX_free(digestContext);

    if (exitCode == EXIT_SUCCESS &&
        bcmp(writtenHashDigest, readHashDigest, HASH_DIGEST_LENGTH) == 0) {
        printf("SUCCESS\n");
    } else {
        printf("FAILURE\n");
        exitCode = EXIT_FAILURE;
    }

#ifdef __APPLE__
    if (kIOReturnSuccess == noIdleSleepAssertionCreated) {
        if (kIOReturnSuccess == IOPMAssertionRelease(noIdleSleepAssertionID)) {
            printf("succesfully released no idle sleep assertion\n");
        } else {
            printf("failed to release no idle sleep assertion\n");
        }
    }
#endif

    for (int i = 0; i < BUFFER_COUNT; i++) {
        free(shared->buffers[i].data);
        pthread_mutex_destroy(&shared->buffers[i].mutex);
        pthread_cond_destroy(&shared->buffers[i].cond);
    }

    free(checkDigests);
    close(fd);

    return exitCode;
}
