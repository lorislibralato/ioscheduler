#ifndef SCHEDULER_H
#define SCHEDULER_H

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include "liburing.h"

#define ENTRIES (1 << 12)
#define PAGE_SZ (1 << 12)
#define BUF_SIZE (PAGE_SZ << 0)

#define TIME_MS(x) (x * 1000 * 1000)

#define WRITE_TIMEOUT_MS (TIME_MS(5))
#define BACKGROUND_STATUS_MS (TIME_MS(200))
#define BACKGROUND_FLUSH_MS (TIME_MS(100))

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

struct page_write_node
{
    struct op_page_write *tail;
    struct op_page_write *head;
};

struct op_page_read
{
    struct op inner;
    unsigned int page_id;
};

struct op_job
{
    struct op inner;
    struct __kernel_timespec ts;
};

struct writer_job
{
    struct op_job op;
    char *buf;
    int fd;
    unsigned int page_id;
    unsigned int batch_size;
    unsigned int written;
    unsigned int inflight;
};

struct reader_job
{
    struct op_job op;
    int fd;
    unsigned int page_id;
    unsigned int batch_size;
};

struct flusher_job
{
    struct op_job op;
    struct op_file_synced op_fsync;
    struct page_write_node write_list;
    int fd;
    int running;
};

struct status_job
{
    struct op_job op;
    struct timespec last_time;
};

struct io_uring_sqe *io_prepare_sqe(struct io_uring *ring, struct op *op, op_callback_t callback);
int io_tick(struct io_uring *ring);

int page_written(struct op *base_op, struct io_uring_cqe *cqe);
int page_read(struct op *base_op, struct io_uring_cqe *cqe);
int file_synced(struct op *base_op, struct io_uring_cqe *cqe);

int create_job(struct io_uring *ring, struct op_job *op, unsigned long nsec, unsigned long sec, op_callback_t callback);
int resend_job(struct io_uring *ring, struct op_job *op);

int background_reader(struct op *base_op, struct io_uring_cqe *cqe);
int background_writer(struct op *base_op, struct io_uring_cqe *cqe);
int background_flusher(struct op *base_op, struct io_uring_cqe *cqe);
int background_status(struct op *base_op, struct io_uring_cqe *cqe);

void background_writer_init(int fd);
void background_reader_init(int fd);
void background_flusher_init(int fd);
void background_status_init();

struct stats_bucket
{
    __u32 *buf;
    __u32 len;
    __u32 acc;
    __u32 idx;
};

void stats_bucket_init(struct stats_bucket *bucket, __u32 len);
void stats_bucket_move(struct stats_bucket *bucket);
void stats_bucket_add(struct stats_bucket *bucket);

extern unsigned int page_id_check_order;

extern struct stats_bucket background_write_stats;
extern struct stats_bucket background_read_stats;

struct thread_context
{
    struct writer_job writer_job;
    struct reader_job reader_job;
    struct status_job status_job;
    struct flusher_job flusher_job;
    struct io_uring_params params;
    struct io_uring ring;
};

extern struct thread_context context;

#endif