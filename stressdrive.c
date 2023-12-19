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
#  define EVP_MD_CTX_new   EVP_MD_CTX_create
#  define EVP_MD_CTX_free  EVP_MD_CTX_destroy
#endif

void DIGEST_Init(EVP_MD_CTX *digestContext) {
    if (1 != EVP_DigestInit_ex(digestContext, EVP_sha1(), NULL)) {
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
    for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
        printf("%02x", digest[i]);
    }
    printf(" <= SHA-1 of %s data\n", name);
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

    uint8_t *buffer = malloc(bufferSize);
    if (buffer == NULL) {
        perror("malloc() failed");
        exit(EXIT_CALL_FAILED);
    }

    uint16_t bufferBlocks = bufferSize / blockSize;
    uint32_t checkFrequency = 1024 * 1024 * 1024 / blockSize;
    uint64_t checkCount = (blockCount + bufferBlocks - 1) / checkFrequency;
    uint8_t *checkDigests = malloc(checkCount * SHA_DIGEST_LENGTH);
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

    int aesKeylength = 128;
    unsigned char aesKey[aesKeylength / 8];
    if (!RAND_bytes(aesKey, aesKeylength / 8)) {
        fprintf(stderr, "RAND_bytes() failed\n");
        exit(EXIT_CALL_FAILED);
    }

    unsigned char aesIv[AES_BLOCK_SIZE];
    if (!RAND_bytes(aesIv, AES_BLOCK_SIZE)) {
        fprintf(stderr, "RAND_bytes() failed\n");
        exit(EXIT_CALL_FAILED);
    }

    EVP_CIPHER_CTX *aes = EVP_CIPHER_CTX_new();
    if (!aes) {
        fprintf(stderr, "EVP_CIPHER_CTX_new() failed\n");
        exit(EXIT_CALL_FAILED);
    }
    if (!EVP_EncryptInit(aes, EVP_aes_128_cbc(), aesKey, aesIv)) {
        fprintf(stderr, "EVP_EncryptInit() failed\n");
        exit(EXIT_CALL_FAILED);
    }

    unsigned char *aesInput = malloc(bufferSize);
    memset(aesInput, 0, bufferSize);

    printf("writing random data to %s\n", drivePath);
    DIGEST_Init(digestContext);
    PROGRESS_Init(&progress, blockCount, "writing");
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        uint32_t size =
            (uint32_t)MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        int outSize;
        if (!EVP_EncryptUpdate(aes, buffer, &outSize, aesInput, size)) {
            fprintf(stderr, "EVP_EncryptUpdate() failed\n");
            exit(EXIT_CALL_FAILED);
        }
        if (outSize != size) {
            fprintf(stderr,
                    "EVP_EncryptUpdate() returned %d instead of %u bytes\n",
                    outSize, size);
            exit(EXIT_CALL_FAILED);
        }

        if (write(fd, buffer, size) != size) {
            perror("write() failed");
            exit(EXIT_CALL_FAILED);
        }
        DIGEST_Update(digestContext, buffer, size);
        PROGRESS_Update(&progress, blockIndex, blockSize);

        if ((blockIndex + bufferBlocks) % checkFrequency == 0) {
          uint64_t checkIndex = blockIndex / checkFrequency;
          DIGEST_Final(digestContext, checkDigests + checkIndex * SHA_DIGEST_LENGTH);
          DIGEST_Init(digestContext);
        }
    }
    PROGRESS_Finish(&progress, blockSize);
    EVP_CIPHER_CTX_free(aes);

    uint8_t writtenShaDigest[SHA_DIGEST_LENGTH];
    DIGEST_Final(digestContext, writtenShaDigest);
    DIGEST_Print(writtenShaDigest, "written");

    if (lseek(fd, 0LL, SEEK_SET) != 0LL) {
        perror("lseek() failed");
        exit(EXIT_CALL_FAILED);
    }

    int exitCode = EXIT_SUCCESS;
    uint8_t readShaDigest[SHA_DIGEST_LENGTH];

    printf("verifying written data\n");
    DIGEST_Init(digestContext);
    PROGRESS_Init(&progress, blockCount, "reading");
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        uint32_t size =
            (uint32_t)MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        if (read(fd, buffer, size) == -1) {
            perror("read() failed");
            exit(EXIT_CALL_FAILED);
        }
        DIGEST_Update(digestContext, buffer, size);
        PROGRESS_Update(&progress, blockIndex, blockSize);

        if ((blockIndex + bufferBlocks) % checkFrequency == 0) {
          uint64_t checkIndex = blockIndex / checkFrequency;
          DIGEST_Final(digestContext, readShaDigest);
          DIGEST_Init(digestContext);
          if (bcmp(checkDigests + checkIndex * SHA_DIGEST_LENGTH, readShaDigest, SHA_DIGEST_LENGTH) != 0) {
            printf("\nFailed intermediate checksum for bytes %" PRIu64 "...%" PRIu64 "\n",
                   (blockIndex + bufferBlocks - checkFrequency) * blockSize,
                   blockIndex * blockSize + size);
            exitCode = EXIT_FAILURE;
          }
        }
    }
    PROGRESS_Finish(&progress, blockSize);
    DIGEST_Final(digestContext, readShaDigest);
    DIGEST_Print(readShaDigest, "read");
    EVP_MD_CTX_free(digestContext);

    if (exitCode == EXIT_SUCCESS && bcmp(writtenShaDigest, readShaDigest, SHA_DIGEST_LENGTH) == 0) {
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

    free(checkDigests);
    free(buffer);
    close(fd);

    return exitCode;
}
