#include "scheduler.h"

unsigned int page_id_check_order = 0;

struct stats_bucket background_write_stats;
struct stats_bucket background_read_stats;

struct thread_context context;

int main(int argc, char *argv[])
{
    assert(argc == 2);

    int fd;
    int ret;

    memset(&context.params, 0, sizeof(context.params));

    context.params.cq_entries = ENTRIES << 1;
    context.params.flags =
        // IORING_SETUP_IOPOLL |
        IORING_SETUP_COOP_TASKRUN |
        IORING_SETUP_TASKRUN_FLAG |
        IORING_SETUP_DEFER_TASKRUN |
        IORING_SETUP_SINGLE_ISSUER |
        IORING_SETUP_NO_SQARRAY |
        IORING_SETUP_CQSIZE;

    ret = io_uring_queue_init_params(ENTRIES, &context.ring, &context.params);
    assert(ret == 0);

    fd = open(argv[1], O_DIRECT | O_RDWR | O_CREAT, 0644);
    assert(fd != -1);

    printf("fd = %d\n", fd);

    // ret = fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, (__u64)(1UL * (1UL << 30)));
    // assert(ret != -1);

#define STATS_BUF_LEN (1000 / BACKGROUND_STATUS_MS)
    stats_bucket_init(&background_write_stats, STATS_BUF_LEN);
    stats_bucket_init(&background_read_stats, STATS_BUF_LEN);

    background_writer_init(fd);
    // background_reader_init(fd);
    background_flusher_init(fd);
    background_status_init();

    printf("setup done!\n");

    while (1)
    {
        ret = io_uring_submit(&context.ring);
        assert(ret >= 0);

        ret = io_tick(&context.ring);
        assert(ret >= 0);
    }

    return 0;
}