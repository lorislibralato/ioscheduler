#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <linux/types.h>
#include <unistd.h>
#include "utils.h"

#define PAGE_SZ (1 << 12)
#define BUF_SIZE ((__u64)PAGE_SZ << 10)
static char buf[BUF_SIZE] = {'a'};

int main(int argc, char *argv[])
{
    ASSERT(argc == 2);

    int fd;
    int ret;

    fd = open(argv[1], O_DIRECT | O_RDWR | O_CREAT, 0644);
    ASSERT(fd != -1);

    LOG("fd = %d\n", fd);

    ret = fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, (__u64)(16UL * (1UL << 30)));
    ASSERT(ret != -1);

    struct timespec start;
    struct timespec now;

    clock_gettime(CLOCK_REALTIME, &start);

    __u64 written = 0;
    while (written < BUF_SIZE)
    {
        written += pwrite(fd, buf + written, BUF_SIZE - written, written);
        printf("w: %llu\n", written);
    }
    ASSERT(written == BUF_SIZE);
    ret = fsync(fd);
    ASSERT(!ret);
    clock_gettime(CLOCK_REALTIME, &now);

    __u64 elapsed = TIME_S(now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec);
    __u64 mbs_speed = (BUF_SIZE) / (elapsed / TIME_S(1)) / (1 << 20);

    printf("written %llu | speed: %llu\n", BUF_SIZE / (1 << 20), mbs_speed);

    return 0;
}
