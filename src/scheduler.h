#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <liburing.h>
#include "cbuf.h"
#include "utils.h"

#define ENTRIES (1 << 14)
#define BUF_SIZE (BYTE_KB(1 << 6))

#define WRITE_TIMEOUT_MS (TIME_MS(5))
#define READ_TIMEOUT_MS (TIME_MS(100))
#define BACKGROUND_STATUS_MS (TIME_MS(200))
#define BACKGROUND_TRACING_MS (TIME_MS(10))
#define BACKGROUND_FLUSH_MS (TIME_MS(100))
#define SPEEDTEST_RANGE_MS (TIME_MS(1500))
#define BATCH_INCREMENT_PERCENT (1)
#define INFLIGHT_LOW_RANGE (8)
#define INFLIGHT_HIGH_RANGE (32)
#define TRACING_BUF_LEN (64)
#define BYTES_TO_WRITE (BYTE_GB(2))

struct op;
typedef int (*op_callback_t)(struct op *op, struct io_uring_cqe *cqe);

struct op
{
    op_callback_t callback;
};

struct op_file_synced
{
    struct op inner;
    struct timespec issued;
};

struct op_page_write
{
    struct op inner;
    unsigned int page_id;
    int (*user_fsync_callback)(void);
    struct op_page_write *next;
    struct timespec issued;
};

struct page_write_node
{
    struct op_page_write *tail;
    struct op_page_write *head;
};

struct op_page_read
{
    struct op inner;
    void *buf;
    struct timespec issued;
    unsigned int page_id;
};

struct op_job
{
    struct op inner;
    struct __kernel_timespec ts;
    int running;
};

struct writer_job
{
    struct op_job inner;
    char *buf;
    int fd;
    __u32 page_id;
    __u32 batch_size;
    __u32 written_no_flush;
    __u32 write_done;
    __u8 inflight;
};

struct reader_job
{
    struct op_job inner;
    int fd;
    __u32 page_id;
    __u32 batch_size;
    __u32 read_done;
    __u8 inflight;
};

struct flusher_job
{
    struct op_job inner;
    struct op_file_synced op_fsync;
    struct page_write_node write_list;
    int fd;
    __u32 page_id;
    __u8 inflight;
};

struct status_job
{
    struct op_job inner;
    struct timespec last_time;
};

struct tracing_item
{
    __u64 ts;
    __u32 flush_page_id;
    __u32 write_inflight;
    __u32 write_page_id;
    __u32 write_batch_size;
    __u32 read_inflight;
    __u32 read_page_id;
    __u32 read_batch_size;
};

struct tracing_dump_op
{
    struct op inner;
    int fd;
    __u32 to_write;
    __u32 items;
    __u8 inflight;
};

struct tracing_job
{
    struct op_job inner;
    struct timespec start_time;
    struct cbuf cbuf;
    __u64 trace_done;
    __u64 trace_synced;
    struct tracing_dump_op dump_op;
};

struct io_uring_sqe *io_prepare_sqe(struct io_uring *ring, struct op *op, op_callback_t callback);
unsigned int io_tick(struct io_uring *ring);

int page_written(struct op *base_op, struct io_uring_cqe *cqe);
int page_read(struct op *base_op, struct io_uring_cqe *cqe);
int file_synced(struct op *base_op, struct io_uring_cqe *cqe);
int tracing_synced(struct op *base_op, struct io_uring_cqe *cqe);
int tracing_writed(struct op *base_op, struct io_uring_cqe *cqe);

void init_job(struct op_job *op, unsigned long nsec, unsigned long sec, op_callback_t callback);
int run_job(struct io_uring *ring, struct op_job *op);
int resend_job(struct io_uring *ring, struct op_job *op);
void job_set_stopped(struct op_job *op);

int background_reader(struct op *base_op, struct io_uring_cqe *cqe);
int background_writer(struct op *base_op, struct io_uring_cqe *cqe);
int background_flusher(struct op *base_op, struct io_uring_cqe *cqe);
int background_status(struct op *base_op, struct io_uring_cqe *cqe);
int background_tracing(struct op *base_op, struct io_uring_cqe *cqe);

void background_writer_init(int fd);
void background_reader_init(int fd);
void background_flusher_init(int fd);
void background_status_init(void);
void background_tracing_init(void);

struct stats_bucket_item
{
    __u64 val;
    __u64 elapsed;
};

struct stats_bucket
{
    struct stats_bucket_item *buf;
    __u64 acc_time;
    __u64 acc_val;
    __u32 len;
    __u32 idx;
};

void stats_bucket_init(struct stats_bucket *bucket, __u32 len);
void stats_bucket_move(struct stats_bucket *bucket, __u64 elapsed);
void stats_bucket_add_one(struct stats_bucket *bucket);
void stats_bucket_add(struct stats_bucket *bucket, __u64 val);

extern unsigned int page_id_check_order;

extern struct stats_bucket background_write_count_stats;
extern struct stats_bucket background_read_count_stats;
extern struct stats_bucket background_write_latency_stats;
extern struct stats_bucket background_read_latency_stats;
extern struct stats_bucket background_fsync_latency_stats;
extern struct stats_bucket background_fsync_count_stats;

struct thread_context
{
    struct writer_job writer_job;
    struct reader_job reader_job;
    struct status_job status_job;
    struct tracing_job tracing_job;
    struct flusher_job flusher_job;
    struct io_uring_params params;
    struct io_uring ring;
};

extern struct thread_context thread_ctx;

#endif
