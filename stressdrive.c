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

int main(int argc, const char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: sudo %s /dev/rdisk6\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    const char *drivePath = argv[1];
    int fd = open(drivePath, O_RDWR);
    if (fd == -1) {
        perror("open(2) failed");
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
	unsigned char shaDigest[SHA_DIGEST_LENGTH];
	SHA1_Init(&shaContext);
    
    struct timeval start;
    gettimeofday(&start, NULL);
    
    uint8_t *block = malloc(blockSize);
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
            printf("\r%.0f%% (block %lld of %lld)",
                   ((double)blockIndex / (double)blockCount) * 100.0,
                   blockIndex,
                   blockCount);
            fflush(stdout);
        }
    }
    free(block);
    printf("\n");
    
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
    SHA1_Final(shaDigest, &shaContext);
    printf("SHA-1: ");
    for (int shaByteIndex = 0; shaByteIndex < SHA_DIGEST_LENGTH; shaByteIndex++) {
        printf("%02x", shaDigest[shaByteIndex]);
    }
    printf("  %s (SHA-1 of written data)\n", drivePath);
    
    return 0;
}
