// stressdrive.c 1.0
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

int main(int argc, const char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: sudo %s /dev/rdisk6\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    const char *drivePath = argv[1];
    int fd = open(drivePath, O_RDWR);
    if (fd == -1) {
        perror("open() failed");
        exit(EXIT_FAILURE);
    }
    
    uint32_t blockSize;
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) == -1) {
        perror("ioctl(DKIOCGETBLOCKSIZE) failed");
        exit(EXIT_FAILURE);
    }
    printf("blockSize: %d\n", blockSize);
    
    uint64_t blockCount;
    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) == -1) {
        perror("ioctl(DKIOCGETBLOCKCOUNT) failed");
        exit(EXIT_FAILURE);
    }
    printf("blockCount: %llu\n", blockCount);
    
    SHA_CTX shaContext;
    struct timeval start;
    gettimeofday(&start, NULL);
    uint8_t *block = malloc(blockSize);
    
    printf("writing random data to %s\n", drivePath);
    SHA1_Init(&shaContext);
    uint8_t writtenShaDigest[SHA_DIGEST_LENGTH];
    for (uint64_t blockIndex = 0; blockIndex < blockCount; blockIndex++) {
        int err = RAND_pseudo_bytes(block, blockSize);
        assert(err == 0 || err == 1);
        
        err = write(fd, block, blockSize);
        if (err != blockSize) {
            perror("write() failed");
            exit(EXIT_FAILURE);
        }
        
        SHA1_Update(&shaContext, block, blockSize);
        
        struct timeval now, delta;
        gettimeofday(&now, NULL);
        timersub(&now, &start, &delta);
        if (delta.tv_sec >= 1) {
            gettimeofday(&start, NULL);
            printf("\rwriting %.0f%% (block %lld of %lld)",
                   ((double)blockIndex / (double)blockCount) * 100.0,
                   blockIndex,
                   blockCount);
            fflush(stdout);
        }
    }
    
    printf("\n");
    SHA1_Final(writtenShaDigest, &shaContext);
    for (int shaByteIndex = 0; shaByteIndex < SHA_DIGEST_LENGTH; shaByteIndex++) {
        printf("%02x", writtenShaDigest[shaByteIndex]);
    }
    printf(" <= SHA-1 of written data\n", drivePath);
    
    if (lseek(fd, 0LL, SEEK_SET) != 0LL) {
        perror("lseek() failed");
        exit(EXIT_FAILURE);
    }
    
    printf("verifying written data\n");
    SHA1_Init(&shaContext);
    uint8_t readShaDigest[SHA_DIGEST_LENGTH];
    for (uint64_t blockIndex = 0; blockIndex < blockCount; blockIndex++) {
        if (read(fd, block, blockSize) == -1) {
            perror("read() failed");
            exit(EXIT_FAILURE);
        }
        SHA1_Update(&shaContext, block, blockSize);
        
        struct timeval now, delta;
        gettimeofday(&now, NULL);
        timersub(&now, &start, &delta);
        if (delta.tv_sec >= 1) {
            gettimeofday(&start, NULL);
            printf("\rreading %.0f%% (block %lld of %lld)",
                   ((double)blockIndex / (double)blockCount) * 100.0,
                   blockIndex,
                   blockCount);
            fflush(stdout);
        }
    }
    
    printf("\n");
    SHA1_Final(readShaDigest, &shaContext);
    for (int shaByteIndex = 0; shaByteIndex < SHA_DIGEST_LENGTH; shaByteIndex++) {
        printf("%02x", readShaDigest[shaByteIndex]);
    }
    printf(" <= SHA-1 of read data\n", drivePath);
    
    if (bcmp(writtenShaDigest, readShaDigest, SHA_DIGEST_LENGTH) == 0) {
        printf("SUCCESS\n");
    } else {
        printf("FAILURE\n");
    }
    
    free(block);
    
    /*
     This ends up kicking the entire device off the bus, which is too much:
     if (ioctl(fd, DKIOCEJECT) == -1) {
         perror("ioctl(DKIOCEJECT) failed");
         exit(EXIT_FAILURE);
     }
     Instead, run `stressdrive foo; diskutil unmount /Volumes/bar` or disable
     Disk Arbitration via 
     `sudo launchctl (unload|load) /System/Library/LaunchDaemons/com.apple.diskarbitrationd.plist`
     */
    
    close(fd);
    return 0;
}
