#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include "utils.h"
#include "scheduler.h"
#include "configure.h"

unsigned int page_id_check_order = 0;

struct stats_bucket background_write_count_stats;
struct stats_bucket background_read_count_stats;
struct stats_bucket background_write_latency_stats;
struct stats_bucket background_read_latency_stats;
struct stats_bucket background_fsync_latency_stats;
struct stats_bucket background_fsync_count_stats;

struct thread_context thread_ctx;

int main(int argc, char *argv[])
{
    ASSERT(argc == 2);

    int fd;
    int ret;

    memset(&thread_ctx.params, 0, sizeof(thread_ctx.params));

    thread_ctx.params.cq_entries = ENTRIES << 1;
    thread_ctx.params.flags =
        // IORING_SETUP_SQPOLL |
        IORING_SETUP_COOP_TASKRUN |
        IORING_SETUP_TASKRUN_FLAG |
        IORING_SETUP_DEFER_TASKRUN |
        IORING_SETUP_SINGLE_ISSUER |
        IORING_SETUP_NO_SQARRAY |
        IORING_SETUP_CQSIZE;

    ret = io_uring_queue_init_params(ENTRIES, &thread_ctx.ring, &thread_ctx.params);
    ASSERT(ret == 0);

    fd = open(argv[1], O_DIRECT | O_RDWR | O_CREAT, 0644);
    ASSERT(fd != -1);

    LOG("fd = %d\n", fd);

    // ret = fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, (__u64)(16UL * (1UL << 30)));
    // ASSERT(ret != -1);

    ASSERT(INFLIGHT_LOW_RANGE <= INFLIGHT_HIGH_RANGE);
    ASSERT(BACKGROUND_STATUS_MS <= SPEEDTEST_RANGE_MS);
    ASSERT(BATCH_INCREMENT_PERCENT >= 0);

#define STATS_BUF_LEN (SPEEDTEST_RANGE_MS / BACKGROUND_STATUS_MS)
    stats_bucket_init(&background_write_count_stats, STATS_BUF_LEN);
    stats_bucket_init(&background_read_count_stats, STATS_BUF_LEN);
    stats_bucket_init(&background_write_latency_stats, STATS_BUF_LEN);
    stats_bucket_init(&background_read_latency_stats, STATS_BUF_LEN);
    stats_bucket_init(&background_fsync_latency_stats, STATS_BUF_LEN);
    stats_bucket_init(&background_fsync_count_stats, STATS_BUF_LEN);

    background_writer_init(fd);
    background_reader_init(fd);
    background_flusher_init(fd);
    background_tracing_init();
    background_status_init();

    run_job(&thread_ctx.ring, &thread_ctx.writer_job.inner);
#ifdef ENABLE_READER
    run_job(&thread_ctx.ring, &thread_ctx.reader_job.inner);
#endif
#ifdef ENABLE_FLUSHER
    run_job(&thread_ctx.ring, &thread_ctx.flusher_job.inner);
#endif
#ifdef ENABLE_TRACING
    run_job(&thread_ctx.ring, &thread_ctx.tracing_job.inner);
#endif
    run_job(&thread_ctx.ring, &thread_ctx.status_job.inner);

    LOG("setup done!\n");

    int submitted;
    int consumed;
    (void)submitted;
    (void)consumed;

    while (1)
    {
        ret = io_uring_submit(&thread_ctx.ring);
        ASSERT(ret >= 0);
        submitted = ret;

        ret = io_tick(&thread_ctx.ring);
        ASSERT(ret >= 0);
        consumed = ret;

        if (!thread_ctx.writer_job.inner.running && !thread_ctx.flusher_job.inner.running && !thread_ctx.reader_job.inner.running && !thread_ctx.tracing_job.inner.running)
            break;
    }

    return 0;
}
