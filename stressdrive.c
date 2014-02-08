// stressdrive.c 1.1
//   Copyright (c) 2011 Jonathan 'Wolf' Rentzsch: http://rentzsch.com
//   Some rights reserved: http://opensource.org/licenses/MIT
//   https://github.com/rentzsch/stressdrive

#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/disk.h>
#include <strings.h>

#ifdef __APPLE__
#import <IOKit/pwr_mgt/IOPMLib.h>
#endif

#define EXIT_CALL_FAILED 2

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
  printf("\r%s %.1f%% (%lld of %lld)", ctx->name, complete * 100.0, current,
         ctx->total);

  uint64_t elapsed = now->tv_sec - ctx->start.tv_sec;
  printf(" %02llu:%02llu:%02llu", elapsed / 3600, (elapsed / 60) % 60,
         elapsed % 60);

  if (current != ctx->total && elapsed > 10 && complete > 0.001) {
    uint64_t eta = (1 / complete - 1) * elapsed;
    printf(" (ETA: %02llu:%02llu:%02llu)", eta / 3600, (eta / 60) % 60,
           eta % 60);
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
    fprintf(stderr, "Usage: sudo %s /dev/rdiskN\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  const char *drivePath = argv[1];
  int fd = open(drivePath, O_RDWR);
  if (fd == -1) {
    perror("open() failed");
    exit(EXIT_CALL_FAILED);
  }

  uint32_t blockSize;
  if (ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) == -1) {
    perror("ioctl(DKIOCGETBLOCKSIZE) failed");
    exit(EXIT_CALL_FAILED);
  }
  printf("blockSize: %d\n", blockSize);

  uint64_t blockCount;
  if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) == -1) {
    perror("ioctl(DKIOCGETBLOCKCOUNT) failed");
    exit(EXIT_CALL_FAILED);
  }
  printf("blockCount: %llu\n", blockCount);

  // For efficiency figure out the max blockSize that still fits in evenly into
  // the
  // drive's capacity:
  uint16_t speedScale = 2;
  while ((blockCount % (uint64_t)speedScale) == 0) {
    speedScale *= 2;
    if (speedScale == 32768) {
      break;
    }
  }
  speedScale /= 2;
  printf("speedScale: %ux\n", speedScale);

  blockSize *= speedScale;
  printf("scaled blockSize: %u\n", blockSize);

  blockCount /= speedScale;
  printf("scaled blockCount: %llu\n", blockCount);

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
  uint8_t *block = malloc(blockSize);

  printf("writing random data to %s\n", drivePath);
  SHA1_Init(&shaContext);
  PROGRESS_Init(&progress, blockCount, "writing");
  for (uint64_t blockIndex = 0; blockIndex < blockCount; blockIndex++) {
    int err = RAND_pseudo_bytes(block, blockSize);
    assert(err == 0 || err == 1);

    err = write(fd, block, blockSize);
    if (err != blockSize) {
      perror("write() failed");
      exit(EXIT_CALL_FAILED);
    }
    SHA1_Update(&shaContext, block, blockSize);
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
  for (uint64_t blockIndex = 0; blockIndex < blockCount; blockIndex++) {
    if (read(fd, block, blockSize) == -1) {
      perror("read() failed");
      exit(EXIT_CALL_FAILED);
    }
    SHA1_Update(&shaContext, block, blockSize);
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

  free(block);
  close(fd);

  return exitCode;
}
