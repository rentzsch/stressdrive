// stressdrive.c 1.2
//   Copyright (c) 2011-2016 Jonathan 'Wolf' Rentzsch: http://rentzsch.com
//   Some rights reserved: http://opensource.org/licenses/MIT
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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/disk.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
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

void _PROGRESS_Print(PROGRESS_CTX *ctx, struct timeval *now, uint64_t current) {
    double complete = (double)current / (double)ctx->total;
    printf("\r%s %.1f%% (%" PRIu64 " of %" PRIu64 ")", ctx->name,
           complete * 100.0, current, ctx->total);

    uint64_t elapsed = now->tv_sec - ctx->start.tv_sec;
    printf(" %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 "", elapsed / 3600,
           (elapsed / 60) % 60, elapsed % 60);

    if (current != ctx->total && elapsed > 10 && complete > 0.001) {
        uint64_t eta = (1 / complete - 1) * elapsed;
        printf(" (ETA: %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ")", eta / 3600,
               (eta / 60) % 60, eta % 60);
    }

    printf("\e[K");
    fflush(stdout);
}

void PROGRESS_Update(PROGRESS_CTX *ctx, uint64_t current) {
    struct timeval now, delta;
    gettimeofday(&now, NULL);
    timersub(&now, &ctx->last_display, &delta);
    if (delta.tv_sec < 1)
        return;
    ctx->last_display = now;
    _PROGRESS_Print(ctx, &now, current);
}

void PROGRESS_Finish(PROGRESS_CTX *ctx) {
    struct timeval now;
    gettimeofday(&now, NULL);
    _PROGRESS_Print(ctx, &now, ctx->total);
    printf("\n");
}

void SHA1_Finish(unsigned char *digest, SHA_CTX *ctx, const char *name) {
    SHA1_Final(digest, ctx);
    for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
        printf("%02x", digest[i]);
    }
    printf(" <= SHA-1 of %s data\n", name);
}

int main(int argc, const char *argv[]) {
    if (argc != 2) {
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

#ifdef __APPLE__
    IOPMAssertionID noIdleAssertionID;
    IOReturn noIdleAssertionCreated = IOPMAssertionCreateWithName(
        kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn,
        CFSTR("stressdrive running"), &noIdleAssertionID);
    if (kIOReturnSuccess == noIdleAssertionCreated) {
        printf("succesfully created no idle assertion\n");
    } else {
        printf("failed to create no idle assertion\n");
    }
#endif

    SHA_CTX shaContext;
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

    EVP_CIPHER_CTX aes;
    if (!EVP_EncryptInit(&aes, EVP_aes_128_cbc(), aesKey, aesIv)) {
        fprintf(stderr, "EVP_EncryptInit() failed\n");
        exit(EXIT_CALL_FAILED);
    }

    unsigned char *aesInput = malloc(bufferSize);
    memset(aesInput, 0, bufferSize);

    printf("writing random data to %s\n", drivePath);
    SHA1_Init(&shaContext);
    PROGRESS_Init(&progress, blockCount, "writing");
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        uint32_t size = MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        int outSize;
        if (!EVP_EncryptUpdate(&aes, buffer, &outSize, aesInput, size)) {
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
        SHA1_Update(&shaContext, buffer, size);
        PROGRESS_Update(&progress, blockIndex);
    }
    PROGRESS_Finish(&progress);

    uint8_t writtenShaDigest[SHA_DIGEST_LENGTH];
    SHA1_Finish(writtenShaDigest, &shaContext, "written");

    if (lseek(fd, 0LL, SEEK_SET) != 0LL) {
        perror("lseek() failed");
        exit(EXIT_CALL_FAILED);
    }

    printf("verifying written data\n");
    SHA1_Init(&shaContext);
    PROGRESS_Init(&progress, blockCount, "reading");
    for (uint64_t blockIndex = 0; blockIndex < blockCount;
         blockIndex += bufferBlocks) {
        uint32_t size = MIN(bufferBlocks, blockCount - blockIndex) * blockSize;

        if (read(fd, buffer, size) == -1) {
            perror("read() failed");
            exit(EXIT_CALL_FAILED);
        }
        SHA1_Update(&shaContext, buffer, size);
        PROGRESS_Update(&progress, blockIndex);
    }
    PROGRESS_Finish(&progress);

    uint8_t readShaDigest[SHA_DIGEST_LENGTH];
    SHA1_Finish(readShaDigest, &shaContext, "read");

    int exitCode;
    if (bcmp(writtenShaDigest, readShaDigest, SHA_DIGEST_LENGTH) == 0) {
        printf("SUCCESS\n");
        exitCode = EXIT_SUCCESS;
    } else {
        printf("FAILURE\n");
        exitCode = EXIT_FAILURE;
    }

#ifdef __APPLE__
    if (kIOReturnSuccess == noIdleAssertionCreated) {
        if (kIOReturnSuccess == IOPMAssertionRelease(noIdleAssertionID)) {
            printf("succesfully released no idle assertion\n");
        } else {
            printf("failed to release no idle assertion\n");
        }
    }
#endif

    free(buffer);
    close(fd);

    return exitCode;
}
