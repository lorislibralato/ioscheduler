#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "liburing.h"

#define ENTRIES (1 << 12)
#define PAGE_SZ (1 << 12)
#define BUF_SIZE (PAGE_SZ << 0)
#define WRITE_TIMEOUT_MS (5)
#define BACKGROUND_STATUS_MS (200)
#define BACKGROUND_FLUSH_MS (100)

struct op;
typedef int (*op_callback_t)(struct op *op, struct io_uring_cqe *cqe);

struct op
{
    op_callback_t callback;
};

struct op_file_synced
{
    struct op inner;
};

struct op_page_write
{
    struct op inner;
    unsigned int page_id;
    int (*user_fsync_callback)();
    struct op_page_write *next;
};

struct op_job
{
    struct op inner;
    struct __kernel_timespec ts;
};

struct op_background_writer
{
    struct op_job inner;
    int fd;
    unsigned int page_id;
    unsigned int batch_size;
};

struct op_background_flusher
{
    struct op_job inner;
    struct op_file_synced op_fsync;
    int fd;
};

struct op_background_status
{
    struct op_job inner;
    struct timespec last_time;
};

struct io_uring_sqe *io_prepare_sqe(struct io_uring *ring, struct op *op, op_callback_t callback);
int io_tick(struct io_uring *ring);
int page_written(struct op *op, struct io_uring_cqe *cqe);
int file_synced(struct op *op, struct io_uring_cqe *cqe);
int create_job(struct io_uring *ring, struct op_job *op, unsigned long nsec, unsigned long msec, unsigned long sec, op_callback_t callback);
int resend_job(struct io_uring *ring, struct op_job *op);

char buf[BUF_SIZE] = {'a'};
unsigned int inflight = 0;
unsigned int batch_size = 1;
unsigned int page_id_check_order = 0;
unsigned int flush_inflight = 0;

unsigned int synced_from_last_status = 0;
unsigned int *written_stats_buf = NULL;
#define WRITTEN_STATS_BUF_LEN (1000 / BACKGROUND_STATUS_MS)
unsigned int written_stats_index = 0;
unsigned int written_accumulator = 0;

struct op_page_write *page_written_tail = NULL;
struct op_page_write *page_written_head = NULL;
unsigned int written = 0;
struct io_uring_params params;
struct io_uring ring;

int io_tick(struct io_uring *ring)
{
    struct __kernel_timespec ts;
    struct io_uring_cqe *cqe;
    struct op *op;
    unsigned int head;
    unsigned int count;
    int ret;

    ts.tv_nsec = 1 * 1000 * 1000;
    ts.tv_sec = 0;

    ret = io_uring_submit_and_wait_timeout(ring, &cqe, 1, &ts, NULL);
    if (ret == -ETIME)
        return 0;
    if (ret < 0)
        return ret;

    count = 0;
    io_uring_for_each_cqe(ring, head, cqe)
    {
        if (cqe->res >= 0 || cqe->res == -ETIME)
        {
            op = (struct op *)cqe->user_data;
            op->callback(op, cqe);
        }
        else
        {
            printf("cqe err: %d - %s\n", -cqe->res, strerror(-cqe->res));
        }

        count++;
    }
    io_uring_cq_advance(ring, count);

    return count;
}

struct io_uring_sqe *io_prepare_sqe(struct io_uring *ring, struct op *op, op_callback_t callback)
{
    struct io_uring_sqe *sqe;
    sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return NULL;

    op->callback = callback;
    io_uring_sqe_set_data(sqe, op);

    return sqe;
}

int page_written(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    assert(base_op);
    struct op_page_write *op = (struct op_page_write *)base_op;
    assert(op->page_id == page_id_check_order);
    page_id_check_order++;

    if (!page_written_tail)
    {
        page_written_tail = op;
        page_written_head = op;
    }
    else
    {
        page_written_head->next = op;
        page_written_head = op;
    }
    written++;
    written_accumulator++;
    written_stats_buf[written_stats_index % WRITTEN_STATS_BUF_LEN]++;
    inflight--;

    return 0;
}

int file_synced(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    assert(base_op);
    struct op_file_synced *op = (struct op_file_synced *)base_op;
    (void)op;

    synced_from_last_status += written;
    written = 0;
    struct op_page_write *head = page_written_tail;
    struct op_page_write *next;
    page_written_tail = page_written_head = NULL;
    while (head)
    {
        if (head->user_fsync_callback)
            head->user_fsync_callback();
        next = head->next;
        free(head);
        head = next;
    }
    flush_inflight = 0;

    return 0;
}

int background_writer(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    assert(base_op);
    struct op_background_writer *op = (struct op_background_writer *)base_op;

    struct io_uring_sqe *sqe;
    struct op_page_write *op_page_write;

    if (inflight <= 16 && batch_size < (ENTRIES >> 1))
    {
        batch_size++;
    }
    else if (batch_size > 0 && inflight >= 64)
    {
        batch_size--;
    }

    __u64 i = 0;
    for (; i < batch_size; i++)
    {
        op_page_write = malloc(sizeof(struct op_page_write));
        assert(op_page_write);
        op_page_write->page_id = op->page_id + i;
        op_page_write->next = NULL;
        op_page_write->user_fsync_callback = NULL;
        sqe = io_prepare_sqe(&ring, &op_page_write->inner, page_written);
        assert(sqe);
        io_uring_prep_write(sqe, op->fd, buf, BUF_SIZE, (__u64)(BUF_SIZE) * ((__u64)op->page_id + i));

        inflight++;
    }
    op->page_id += batch_size;

    int ret = resend_job(&ring, &op->inner);
    assert(!ret);

    return 0;
}

int background_flusher(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    assert(base_op);
    struct op_background_flusher *op = (struct op_background_flusher *)base_op;

    struct io_uring_sqe *sqe;

    if (written && !flush_inflight)
    {
        sqe = io_prepare_sqe(&ring, (struct op *)&op->op_fsync, file_synced);
        assert(sqe);
        io_uring_prep_fsync(sqe, op->fd, 0);
        flush_inflight = 1;
    }

    int ret = resend_job(&ring, &op->inner);
    assert(!ret);

    return 0;
}

int background_status(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    assert(base_op);

    struct op_background_status *op = (struct op_background_status *)base_op;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    unsigned long long elapsed_ms = ((now.tv_sec - op->last_time.tv_sec) * 1000 + (now.tv_nsec - op->last_time.tv_nsec) / 1000 / 1000);
    unsigned long long drift_ns = ((now.tv_sec - op->last_time.tv_sec - op->inner.ts.tv_sec) * 1000 * 1000 * 1000 + (now.tv_nsec - op->last_time.tv_nsec - op->inner.ts.tv_nsec));
    (void)drift_ns;

    unsigned long long mbs_speed = (BUF_SIZE * written_accumulator) / (1 << 20);

    printf("\33[2K\r\tinflight: %u | iops: %u | mb/s: %llu | batch: %u | elapsed: %llu ms",
           inflight, written_accumulator, mbs_speed, batch_size, elapsed_ms);
    fflush(stdout);

    synced_from_last_status = 0;
    unsigned int next_idx = ++written_stats_index % WRITTEN_STATS_BUF_LEN;
    written_accumulator -= written_stats_buf[next_idx];
    written_stats_buf[next_idx] = 0;
    op->last_time = now;

    int ret = resend_job(&ring, &op->inner);
    assert(!ret);
    return 0;
}

int create_job(struct io_uring *ring, struct op_job *op, unsigned long nsec, unsigned long msec, unsigned long sec, op_callback_t callback)
{
    struct io_uring_sqe *sqe;
    sqe = io_prepare_sqe(ring, &op->inner, callback);
    if (!sqe)
        return 1;

    op->ts.tv_nsec = msec * 1000 * 1000 + nsec;
    op->ts.tv_sec = sec;
    io_uring_prep_timeout(sqe, &op->ts, 0, 0);
    return 0;
}

int resend_job(struct io_uring *ring, struct op_job *op)
{
    struct io_uring_sqe *sqe;
    sqe = io_prepare_sqe(ring, &op->inner, op->inner.callback);
    if (!sqe)
        return 1;

    io_uring_prep_timeout(sqe, &op->ts, 0, 0);
    return 0;
}

int main(int argc, char *argv[])
{
    assert(argc == 2);

    int fd;
    int ret;

    memset(&params, 0, sizeof(params));

    params.cq_entries = ENTRIES << 1;
    params.flags =
        // IORING_SETUP_IOPOLL |
        IORING_SETUP_COOP_TASKRUN |
        IORING_SETUP_TASKRUN_FLAG |
        IORING_SETUP_DEFER_TASKRUN |
        IORING_SETUP_SINGLE_ISSUER |
        IORING_SETUP_NO_SQARRAY |
        IORING_SETUP_CQSIZE;

    ret = io_uring_queue_init_params(ENTRIES, &ring, &params);
    assert(ret == 0);

    fd = open(argv[1], O_DIRECT | O_RDWR | O_CREAT, 0644);
    assert(fd != -1);

    // ret = fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, (__u64)(1UL * (1UL << 30)));
    // assert(ret != -1);

    struct op_background_writer *op_background_writer = malloc(sizeof(struct op_background_writer));
    assert(op_background_writer);
    op_background_writer->fd = fd;
    op_background_writer->page_id = 0;
    written_stats_buf = calloc(sizeof(__u32), WRITTEN_STATS_BUF_LEN);
    assert(written_stats_buf);

    ret = create_job(&ring, &op_background_writer->inner, 0, WRITE_TIMEOUT_MS, 0, background_writer);
    assert(!ret);

    struct op_background_flusher *op_background_flusher = malloc(sizeof(struct op_background_flusher));
    assert(op_background_flusher);
    op_background_flusher->fd = fd;
    ret = create_job(&ring, &op_background_flusher->inner, 0, BACKGROUND_FLUSH_MS, 0, background_flusher);
    assert(!ret);

    struct op_background_status *op_background_status = malloc(sizeof(struct op_background_status));
    assert(op_background_status);
    ret = create_job(&ring, &op_background_status->inner, 0, BACKGROUND_STATUS_MS, 0, background_status);
    assert(!ret);

    while (1)
    {
        ret = io_uring_submit(&ring);
        assert(ret >= 0);

        ret = io_tick(&ring);
        assert(ret >= 0);
    }

    return 0;
}