#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <liburing.h>
#include "scheduler.h"
#include "utils.h"
#include "cbuf.h"

#define BUF_BYTE ('a')

static char write_buf[BUF_SIZE] = {BUF_BYTE};
static char read_buf[BUF_SIZE] = {BUF_BYTE};

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
            // LOG("cqe_res = %d | func = %p\n", cqe->res, op->callback);

            if (cqe->res == -ETIME)
                ASSERT(op->callback == background_flusher ||
                       op->callback == background_status ||
                       op->callback == background_writer ||
                       op->callback == background_reader ||
                       op->callback == background_tracing);
            ASSERT(op);
            op->callback(op, cqe);
        }
        else
        {
            LOG("cqe err: %d - %s\n", -cqe->res, strerror(-cqe->res));
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
    io_uring_sqe_set_data(sqe, (void *)op);

    return sqe;
}

int page_written(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_page_write *op = (struct op_page_write *)base_op;
    ASSERT(op);
    ASSERT(op->page_id == page_id_check_order);
    page_id_check_order++;

    struct timespec now;
    int ret = clock_gettime(CLOCK_REALTIME, &now);
    ASSERT(!ret);

    struct page_write_node *list = &context.flusher_job.write_list;
    if (!list->tail)
    {
        list->tail = list->head = op;
    }
    else
    {
        list->head->next = op;
        list->head = op;
    }

    context.writer_job.written_no_flush++;
    context.writer_job.inflight--;
    stats_bucket_add_one(&background_write_count_stats);

    __u64 elapsed_us = (TIME_S(now.tv_sec - op->issued.tv_sec) + (now.tv_nsec - op->issued.tv_nsec)) / TIME_US(1);
    stats_bucket_add(&background_write_latency_stats, elapsed_us);

    return 0;
}

int page_read(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_page_read *op = (struct op_page_read *)base_op;
    ASSERT(op);

    struct timespec now;
    int ret = clock_gettime(CLOCK_REALTIME, &now);
    ASSERT(!ret);

    ASSERT(memcmp(write_buf, op->buf, 8) == 0);
    // ASSERT(memcmp(write_buf, op->buf, BUF_SIZE) == 0);

    context.reader_job.inflight--;
    stats_bucket_add_one(&background_read_count_stats);

    __u64 elapsed_us = (TIME_S(now.tv_sec - op->issued.tv_sec) + (now.tv_nsec - op->issued.tv_nsec)) / TIME_US(1);
    stats_bucket_add(&background_read_latency_stats, elapsed_us);

    if (op->buf != read_buf)
        free(op->buf);

    free(op);

    return 0;
}

int tracing_writed(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct tracing_dump_op *op = container_of(base_op, struct tracing_dump_op, inner);
    ASSERT(op);

    ASSERT(cqe->res >= 0);
    ASSERT((__u32)cqe->res == op->to_write);
    cbuf_advance_head(&context.tracing_job.cbuf, op->items);

    struct io_uring_sqe *sqe;

    sqe = io_prepare_sqe(&context.ring, &op->inner, tracing_synced);
    ASSERT(sqe);
    io_uring_prep_fsync(sqe, op->fd, 0);

    return 0;
}

int tracing_synced(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct tracing_dump_op *op = container_of(base_op, struct tracing_dump_op, inner);
    ASSERT(op);

    context.tracing_job.trace_synced += op->to_write;
    op->inflight = 0;

    if (!context.writer_job.op.running && !context.flusher_job.op.running && !context.reader_job.op.running)
        context.tracing_job.op.running = 0;

    return 0;
}

int file_synced(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_file_synced *op = (struct op_file_synced *)base_op;
    ASSERT(op);

    struct timespec now;
    int ret = clock_gettime(CLOCK_REALTIME, &now);
    ASSERT(!ret);

    context.flusher_job.page_id += context.writer_job.written_no_flush;
    context.writer_job.written_no_flush = 0;

    struct op_page_write *node = context.flusher_job.write_list.tail;
    struct op_page_write *next;
    context.flusher_job.write_list.tail = context.flusher_job.write_list.head = NULL;
    while (node)
    {
        if (node->user_fsync_callback)
            node->user_fsync_callback();
        next = node->next;
        free(node);
        node = next;
    }
    context.flusher_job.running = 0;

    stats_bucket_add_one(&background_fsync_count_stats);
    __u64 elapsed_us = (TIME_S(now.tv_sec - op->issued.tv_sec) + (now.tv_nsec - op->issued.tv_nsec)) / TIME_US(1);
    stats_bucket_add(&background_fsync_latency_stats, elapsed_us);

    return 0;
}

int background_reader(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct reader_job *op = (struct reader_job *)base_op;
    ASSERT(op);

    struct io_uring_sqe *sqe;
    struct op_page_read *op_page_read;
    int ret;

    if (op->inflight <= INFLIGHT_LOW_RANGE && op->batch_size < (ENTRIES >> 1))
    {
        if (op->inflight == 0 && op->batch_size > 0)
            op->batch_size *= 2;
        else
            op->batch_size += max(1, op->batch_size * BATCH_INCREMENT_PERCENT / 100);
    }
    else if (op->batch_size > 0 && op->inflight >= INFLIGHT_HIGH_RANGE)
        op->batch_size -= max(1, op->batch_size * BATCH_INCREMENT_PERCENT / 100);

    // limit batch of read to the last page id written
    __u64 limit_page_id;
#ifdef ENABLE_FLUSHER
    limit_page_id = context.flusher_job.page_id;
#else
    limit_page_id = context.writer_job.page_id;
#endif

    op->batch_size = min(op->batch_size, limit_page_id - op->page_id);

    for (__u32 i = 0; i < op->batch_size; i++)
    {
        op_page_read = malloc(sizeof(struct op_page_read));
        ASSERT(op_page_read);
        // op_page_read->buf = malloc(BUF_SIZE);
        op_page_read->buf = read_buf;
        // ASSERT(op_page_read->buf);
        op_page_read->page_id = op->page_id + i;
        ret = clock_gettime(CLOCK_REALTIME, &op_page_read->issued);
        ASSERT(!ret);
        sqe = io_prepare_sqe(&context.ring, &op_page_read->inner, page_read);
        ASSERT(sqe);
        io_uring_prep_read(sqe, op->fd, op_page_read->buf, BUF_SIZE, (__u64)(BUF_SIZE) * ((__u64)op_page_read->page_id));
        // LOG("read op: %p\n", op_page_read);
    }

    op->inflight += op->batch_size;
    op->page_id += op->batch_size;

    if (op->page_id * BUF_SIZE < BYTES_TO_WRITE)
    {
        ret = resend_job(&context.ring, &op->op);
        ASSERT(!ret);
    }
    else
        op->op.running = 0;

    return 0;
}

int background_writer(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct writer_job *op = (struct writer_job *)base_op;
    ASSERT(op);

    struct io_uring_sqe *sqe;
    struct op_page_write *op_page_write;
    int ret;

    if (op->inflight <= INFLIGHT_LOW_RANGE && op->batch_size < (ENTRIES >> 1))
    {
        if (op->inflight == 0 && op->batch_size > 0)
            op->batch_size *= 2;
        else
            op->batch_size += max(1, op->batch_size * BATCH_INCREMENT_PERCENT / 100);
    }
    else if (op->batch_size > 0 && op->inflight >= INFLIGHT_HIGH_RANGE)
        op->batch_size -= max(1, op->batch_size * BATCH_INCREMENT_PERCENT / 100);

    for (__u64 i = 0; i < op->batch_size; i++)
    {
        op_page_write = malloc(sizeof(struct op_page_write));
        ASSERT(op_page_write);
        op_page_write->page_id = op->page_id + i;
        op_page_write->next = NULL;
        op_page_write->user_fsync_callback = NULL;
        ret = clock_gettime(CLOCK_REALTIME, &op_page_write->issued);
        ASSERT(!ret);
        sqe = io_prepare_sqe(&context.ring, &op_page_write->inner, page_written);
        ASSERT(sqe);
        io_uring_prep_write(sqe, op->fd, op->buf, BUF_SIZE, (__u64)(BUF_SIZE) * ((__u64)op_page_write->page_id));
        // LOG("write op: %p\n", op_page_write);
    }
    op->inflight += op->batch_size;
    op->page_id += op->batch_size;

    if (op->page_id * BUF_SIZE < BYTES_TO_WRITE)
    {
        ret = resend_job(&context.ring, &op->op);
        ASSERT(!ret);
    }
    else
        op->op.running = 0;

    return 0;
}

int background_flusher(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct flusher_job *op = (struct flusher_job *)base_op;
    ASSERT(op);

    struct io_uring_sqe *sqe;
    int ret;

    if (context.writer_job.written_no_flush && !op->running)
    {
        ret = clock_gettime(CLOCK_REALTIME, &op->op_fsync.issued);
        ASSERT(!ret);
        sqe = io_prepare_sqe(&context.ring, (struct op *)&op->op_fsync, file_synced);
        ASSERT(sqe);
        io_uring_prep_fsync(sqe, op->fd, 0);
        op->running = 1;
    }

    if (op->page_id * BUF_SIZE < BYTES_TO_WRITE)
    {
        ret = resend_job(&context.ring, &op->op);
        ASSERT(!ret);
    }
    else
        op->op.running = 0;

    return 0;
}

void dump_tracing_items(struct tracing_job *op)
{
    struct io_uring_sqe *sqe;
    struct tracing_item *buf;

    ASSERT(!op->dump_op.inflight);

    __u32 items = cbuf_get(&op->cbuf, (void **)&buf);

    if (items)
    {
        __u32 to_write = items * sizeof(*buf);
        op->dump_op.items = items;
        op->dump_op.to_write = to_write;
        op->dump_op.inflight = 1;

        sqe = io_prepare_sqe(&context.ring, &op->dump_op.inner, tracing_writed);
        ASSERT(sqe);
        io_uring_prep_write(sqe, op->dump_op.fd, buf, to_write, op->trace_synced);
    }
}

int background_tracing(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    int ret;

    struct tracing_job *op = (struct tracing_job *)base_op;
    ASSERT(op);

    struct tracing_item *item;
    __u32 free_space = cbuf_put(&op->cbuf, (void **)&item);

    if (free_space)
    {
        struct timespec now;
        ret = clock_gettime(CLOCK_REALTIME, &now);
        ASSERT(!ret);

        __u64 elapsed = (TIME_S(now.tv_sec) - op->start_time.tv_sec) + (now.tv_nsec - op->start_time.tv_nsec);

        item->ts = elapsed;
        item->flush_page_id = context.flusher_job.page_id;
        item->write_page_id = context.writer_job.page_id;
        item->write_batch_size = context.writer_job.batch_size;
        item->write_inflight = context.writer_job.inflight;
        item->read_inflight = context.reader_job.inflight;
        item->read_batch_size = context.reader_job.batch_size;
        item->read_page_id = context.reader_job.page_id;
        cbuf_advance_tail(&op->cbuf, 1);
        op->trace_done++;
    }

    if (cbuf_free_count(&op->cbuf) <= op->cbuf.len / 2)
        dump_tracing_items(op);

    if (context.writer_job.op.running || context.flusher_job.op.running || context.reader_job.op.running || op->dump_op.inflight)
    {
        ret = resend_job(&context.ring, &op->op);
        ASSERT(!ret);
    }
    else
    {
        dump_tracing_items(op);
    }

    return 0;
}

int background_status(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    int ret;

    struct status_job *op = (struct status_job *)base_op;
    ASSERT(op);

    struct timespec now;
    ret = clock_gettime(CLOCK_REALTIME, &now);
    ASSERT(!ret);

    __u64 elapsed = (TIME_S(now.tv_sec - op->last_time.tv_sec) + (now.tv_nsec - op->last_time.tv_nsec));
    __u64 drift_ns = (TIME_S(now.tv_sec - op->last_time.tv_sec - op->op.ts.tv_sec) + (now.tv_nsec - op->last_time.tv_nsec - op->op.ts.tv_nsec));
    (void)drift_ns;

#ifdef ENABLE_STATUS
    __u64 w_mbs_speed = background_write_count_stats.acc_time ? (BUF_SIZE * background_write_count_stats.acc_val) * TIME_S(1) / background_write_count_stats.acc_time / BYTE_MB(1) : 0;
    __u64 r_mbs_speed = background_read_count_stats.acc_time ? (BUF_SIZE * background_read_count_stats.acc_val) * TIME_S(1) / background_read_count_stats.acc_time / BYTE_MB(1) : 0;
    __u64 r_latency = background_read_count_stats.acc_val ? background_read_latency_stats.acc_val / background_read_count_stats.acc_val * TIME_US(1) / TIME_MS(1) : 0;
    __u64 w_latency = background_write_count_stats.acc_val ? background_write_latency_stats.acc_val / background_write_count_stats.acc_val * TIME_US(1) / TIME_MS(1) : 0;
    __u64 f_latency = background_fsync_count_stats.acc_val ? background_fsync_latency_stats.acc_val / background_fsync_count_stats.acc_val * TIME_US(1) / TIME_MS(1) : 0;

    printf("\33[2K\r inflight:r(%.4u)/w(%.4u) | iops:r(%.5llu)/w(%.5llu) mb/s:r(%.4llu)/w(%.4llu) | batch:r(%.5u)/w(%.5u) | pid:r(%.7u)/w(%.7u)/f(%.7u) | lat:r(%.3llu)/w(%.3llu)/f(%.3llu)ms | elapsed:r(%.5llu)/w(%.5llu) ms",
           context.reader_job.inflight, context.writer_job.inflight,
           background_read_count_stats.acc_val, background_write_count_stats.acc_val,
           r_mbs_speed, w_mbs_speed,
           context.reader_job.batch_size, context.writer_job.batch_size,
           context.reader_job.page_id, context.writer_job.page_id, context.flusher_job.page_id,
           r_latency, w_latency, f_latency,
           background_read_count_stats.acc_time / TIME_MS(1), background_write_count_stats.acc_time / TIME_MS(1));
    fflush(stdout);
#endif

    stats_bucket_move(&background_write_count_stats, elapsed);
    stats_bucket_move(&background_read_count_stats, elapsed);
    stats_bucket_move(&background_write_latency_stats, elapsed);
    stats_bucket_move(&background_read_latency_stats, elapsed);
    stats_bucket_move(&background_fsync_latency_stats, elapsed);
    stats_bucket_move(&background_fsync_count_stats, elapsed);
    op->last_time = now;

    ret = resend_job(&context.ring, &op->op);
    ASSERT(!ret);
    return 0;
}

void background_writer_init(int fd)
{
    context.writer_job.buf = write_buf;
    context.writer_job.fd = fd;
    context.writer_job.page_id = 0;
    context.writer_job.inflight = 0;
    context.writer_job.written_no_flush = 0;
    context.writer_job.batch_size = 0;
    context.writer_job.write_done = 0;
    init_job(&context.writer_job.op, WRITE_TIMEOUT_MS, 0, background_writer);
    ASSERT(context.writer_job.op.inner.callback == background_writer);
    LOG("created writed job: %p\n", background_writer);
}

void background_reader_init(int fd)
{
    context.reader_job.fd = fd;
    context.reader_job.batch_size = 0;
    context.reader_job.inflight = 0;
    context.reader_job.page_id = 0;
    context.reader_job.read_done = 0;
    init_job(&context.reader_job.op, READ_TIMEOUT_MS, 0, background_reader);
    LOG("created reader job: %p\n", background_reader);
}

void background_flusher_init(int fd)
{
    context.flusher_job.fd = fd;
    context.flusher_job.running = 0;
    context.flusher_job.page_id = 0;
    context.flusher_job.write_list.head = context.flusher_job.write_list.tail = NULL;
    init_job(&context.flusher_job.op, BACKGROUND_FLUSH_MS, 0, background_flusher);
    ASSERT(context.flusher_job.op.inner.callback == background_flusher);

    LOG("created flusher job: %p\n", background_flusher);
}

void background_tracing_init(void)
{
    int ret;
    ret = clock_gettime(CLOCK_REALTIME, &context.tracing_job.start_time);
    ASSERT(!ret);

    int fd = open("trace.dat", O_RDWR | O_CREAT, 0644);
    ASSERT(fd > 0);

    ASSERT(TRACING_BUF_LEN % 2 == 0);
    cbuf_init(&context.tracing_job.cbuf, sizeof(struct tracing_item), TRACING_BUF_LEN);

    context.tracing_job.dump_op.fd = fd;
    context.tracing_job.dump_op.inflight = 0;
    context.tracing_job.trace_done = 0;
    context.tracing_job.trace_synced = 0;

    init_job(&context.tracing_job.op, BACKGROUND_TRACING_MS, 0, background_tracing);
    ASSERT(context.tracing_job.op.inner.callback == background_tracing);

    LOG("created status job: %p\n", background_tracing);
}

void background_status_init(void)
{
    int ret;
    ret = clock_gettime(CLOCK_REALTIME, &context.status_job.last_time);
    ASSERT(!ret);

    init_job(&context.status_job.op, BACKGROUND_STATUS_MS, 0, background_status);
    ASSERT(context.status_job.op.inner.callback == background_status);

    LOG("created status job: %p\n", background_status);
}

void init_job(struct op_job *op, unsigned long nsec, unsigned long sec, op_callback_t callback)
{
    op->inner.callback = callback;
    op->ts.tv_nsec = nsec;
    op->ts.tv_sec = sec;
    op->running = 0;
    LOG("created job for: %p\n", callback);
}

int run_job(struct io_uring *ring, struct op_job *op)
{
    int err = resend_job(ring, op);
    if (!err)
        op->running = 1;

    return err;
}

int resend_job(struct io_uring *ring, struct op_job *op)
{
    struct io_uring_sqe *sqe;
    sqe = io_prepare_sqe(ring, &op->inner, op->inner.callback);
    if (!sqe)
        return 1;

    io_uring_prep_timeout(sqe, &op->ts, 0, 0);
    // LOG("resend job for: %p\n", op->inner.callback);
    return 0;
}

void stats_bucket_init(struct stats_bucket *bucket, __u32 len)
{
    bucket->acc_val = 0;
    bucket->idx = 0;
    bucket->acc_time = 0;
    bucket->len = len;
    bucket->buf = calloc(len, sizeof(struct stats_bucket_item));
    ASSERT(bucket->buf);
    // TODO: check calloc error
}

void stats_bucket_add_one(struct stats_bucket *bucket)
{
    stats_bucket_add(bucket, 1);
}

void stats_bucket_move(struct stats_bucket *bucket, __u64 elapsed)
{
    unsigned int next_idx = (++bucket->idx) % bucket->len;
    bucket->acc_val -= bucket->buf[next_idx].val;
    bucket->acc_time -= bucket->buf[next_idx].elapsed;
    bucket->acc_time += elapsed;
    bucket->buf[next_idx].val = 0;
    bucket->buf[next_idx].elapsed = elapsed;
}

void stats_bucket_add(struct stats_bucket *bucket, __u64 val)
{
    bucket->acc_val += val;
    bucket->buf[bucket->idx % bucket->len].val += val;
}
