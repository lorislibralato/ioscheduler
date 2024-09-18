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

static char buf[BUF_SIZE] = {'a'};

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
            LOG("cqe_res = %d | func = %p\n", cqe->res, op->callback);

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
    stats_bucket_add(&background_write_stats);

    return 0;
}

int page_read(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_page_read *op = (struct op_page_read *)base_op;
    ASSERT(op);

    ASSERT(memcmp(buf, op->buf, 8) == 0);
    ASSERT(memcmp(buf, op->buf, BUF_SIZE) == 0);

    context.reader_job.inflight--;
    stats_bucket_add(&background_read_stats);

    if (op->buf != buf)
        free(op->buf);

    free(op);

    return 0;
}

int file_synced(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_file_synced *op = (struct op_file_synced *)base_op;
    ASSERT(op);

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

    return 0;
}

int background_reader(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct reader_job *op = (struct reader_job *)base_op;
    ASSERT(op);

    struct io_uring_sqe *sqe;
    struct op_page_read *op_page_read;

    if (op->inflight <= INFLIGHT_LOW_RANGE && op->batch_size < (ENTRIES >> 1))
    {
        if (op->inflight == 0 && op->batch_size > 0 && op->inflight < INFLIGHT_HIGH_RANGE)
            op->batch_size *= 2;
        else
            op->batch_size += max(1, op->batch_size * BATCH_INCREMENT_PERCENT / 100);
    }
    else if (op->batch_size > 0 && op->inflight >= INFLIGHT_HIGH_RANGE)
        op->batch_size -= max(1, op->batch_size * BATCH_INCREMENT_PERCENT / 100);

    // limit batch of read to the last page id written
    op->batch_size = min(op->batch_size, context.flusher_job.page_id - op->page_id);

    for (__u32 i = 0; i < op->batch_size; i++)
    {
        op_page_read = malloc(sizeof(struct op_page_read));
        ASSERT(op_page_read);
        // op_page_read->buf = malloc(BUF_SIZE);
        op_page_read->buf = buf;
        ASSERT(op_page_read->buf);
        op_page_read->page_id = op->page_id + i;
        sqe = io_prepare_sqe(&context.ring, &op_page_read->inner, page_read);
        ASSERT(sqe);
        io_uring_prep_read(sqe, op->fd, op_page_read->buf, BUF_SIZE, (__u64)(BUF_SIZE) * ((__u64)op_page_read->page_id));
        LOG("read op: %p\n", op_page_read);
    }

    op->inflight += op->batch_size;
    op->page_id += op->batch_size;

    int ret = resend_job(&context.ring, &op->op);
    ASSERT(!ret);

    return 0;
}

int background_writer(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct writer_job *op = (struct writer_job *)base_op;
    ASSERT(op);

    struct io_uring_sqe *sqe;
    struct op_page_write *op_page_write;

    if (op->inflight <= INFLIGHT_LOW_RANGE && op->batch_size < (ENTRIES >> 1) && op->inflight < INFLIGHT_HIGH_RANGE)
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
        sqe = io_prepare_sqe(&context.ring, &(op_page_write->inner), page_written);
        ASSERT(sqe);
        io_uring_prep_write(sqe, op->fd, op->buf, BUF_SIZE, (__u64)(BUF_SIZE) * ((__u64)op_page_write->page_id));
        LOG("write op: %p\n", op_page_write);
    }
    op->inflight += op->batch_size;
    op->page_id += op->batch_size;

    int ret = resend_job(&context.ring, &op->op);
    ASSERT(!ret);

    return 0;
}

int background_flusher(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct flusher_job *op = (struct flusher_job *)base_op;
    ASSERT(op);

    struct io_uring_sqe *sqe;

    if (context.writer_job.written_no_flush && !op->running)
    {
        sqe = io_prepare_sqe(&context.ring, (struct op *)&op->op_fsync, file_synced);
        ASSERT(sqe);
        io_uring_prep_fsync(sqe, op->fd, 0);
        op->running = 1;
    }

    int ret = resend_job(&context.ring, &op->op);
    ASSERT(!ret);

    return 0;
}

int background_tracing(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    int ret;

    struct tracing_job *op = (struct tracing_job *)base_op;
    ASSERT(op);

    if (op->tail == op->head)
    {
        int fd = open("trace.dat", O_RDWR | O_CREAT, 0644);
        ASSERT(fd > 0);

        __u32 written = 0;
        int ret_n;

        char *first_half = (char *)(op->buf + (op->head % op->buf_len));
        __u32 first_len = op->buf_len - (op->head % op->buf_len);

        char *second_half = (char *)op->buf;
        __u32 second_len = op->tail % op->buf_len;
        ASSERT(first_len + second_len == op->buf_len);

        __u32 last_offset = op->trace_synced * sizeof(struct tracing_item);

        while (written < first_len + second_len)
        {
            if (written < first_len)
                ret_n = pwrite(fd, first_half + written, first_len - written, last_offset);
            else
                ret_n = pwrite(fd, second_half + written - first_len, first_len + second_len - written, last_offset);

            ASSERT(ret_n > 0);
            written += ret_n;
            last_offset += ret_n;
        }

        ret = fsync(fd);
        ASSERT(!ret);

        op->trace_synced += op->buf_len - (op->head - op->tail);
        op->head += op->buf_len - (op->head - op->tail);

        ret = close(fd);
        ASSERT(!ret);
        ASSERT(written == op->buf_len);
    }

    struct timespec now;
    ret = clock_gettime(CLOCK_REALTIME, &now);
    ASSERT(!ret);

    __u64 elapsed = (TIME_S(now.tv_sec) - op->start_time.tv_sec) + (now.tv_nsec - op->start_time.tv_nsec);

    struct tracing_item *item = &op->buf[op->tail % op->buf_len];
    item->ts = elapsed;
    item->flush_page_id = context.flusher_job.page_id;
    item->write_page_id = context.writer_job.page_id;
    item->write_batch_size = context.writer_job.batch_size;
    item->write_inflight = context.writer_job.inflight;
    item->read_inflight = context.reader_job.inflight;
    item->read_batch_size = context.reader_job.batch_size;
    item->read_page_id = context.reader_job.page_id;

    op->tail++;
    op->trace_done++;

    ret = resend_job(&context.ring, &op->op);
    ASSERT(!ret);
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

    __u64 w_mbs_speed;
    __u64 r_mbs_speed;

    if (background_write_stats.acc_time)
        w_mbs_speed = (BUF_SIZE * background_write_stats.acc_val) * TIME_S(1) / background_write_stats.acc_time / (1 << 20);
    else
        w_mbs_speed = 0;

    if (background_read_stats.acc_time)
        r_mbs_speed = (BUF_SIZE * background_read_stats.acc_val) * TIME_S(1) / background_read_stats.acc_time / (1 << 20);
    else
        r_mbs_speed = 0;

    printf("\33[2K\r inflight: r(%u)/w(%u) | iops: r(%llu)/w(%llu) | mb/s: r(%llu)/w(%llu) | batch: r(%u)/w(%u) | page_id: r(%u)/w(%u)/f(%u) | elapsed: r(%llu)/w(%llu) ms",
           context.reader_job.inflight, context.writer_job.inflight,
           background_write_stats.acc_val, background_read_stats.acc_val,
           r_mbs_speed, w_mbs_speed,
           context.reader_job.batch_size, context.writer_job.batch_size,
           context.reader_job.page_id, context.writer_job.page_id, context.flusher_job.page_id,
           background_read_stats.acc_time / TIME_MS(1), background_write_stats.acc_time / TIME_MS(1));
    fflush(stdout);

    stats_bucket_move(&background_write_stats, elapsed);
    stats_bucket_move(&background_read_stats, elapsed);
    op->last_time = now;

    ret = resend_job(&context.ring, &op->op);
    ASSERT(!ret);
    return 0;
}

void background_writer_init(int fd)
{
    int ret;

    context.writer_job.buf = buf;
    context.writer_job.fd = fd;
    context.writer_job.page_id = 0;
    context.writer_job.inflight = 0;
    context.writer_job.written_no_flush = 0;
    context.writer_job.batch_size = 0;
    context.writer_job.write_done = 0;
    ret = create_job(&context.ring, &context.writer_job.op, WRITE_TIMEOUT_MS, 0, background_writer);
    ASSERT(!ret);
    ASSERT(context.writer_job.op.inner.callback == background_writer);
    LOG("created writed job: %p\n", background_writer);
}

void background_reader_init(int fd)
{
    int ret;
    context.reader_job.fd = fd;
    context.reader_job.batch_size = 0;
    context.reader_job.inflight = 0;
    context.reader_job.page_id = 0;
    context.reader_job.read_done = 0;
    ret = create_job(&context.ring, &context.reader_job.op, READ_TIMEOUT_MS, 0, background_reader);
    ASSERT(!ret);
    LOG("created reader job: %p\n", background_reader);
}

void background_flusher_init(int fd)
{
    int ret;
    context.flusher_job.fd = fd;
    context.flusher_job.running = 0;
    context.flusher_job.page_id = 0;
    context.flusher_job.write_list.head = context.flusher_job.write_list.tail = NULL;
    ret = create_job(&context.ring, &context.flusher_job.op, BACKGROUND_FLUSH_MS, 0, background_flusher);
    ASSERT(!ret);
    ASSERT(context.flusher_job.op.inner.callback == background_flusher);

    LOG("created flusher job: %p\n", background_flusher);
}

void background_tracing_init()
{
    int ret;

    ret = clock_gettime(CLOCK_REALTIME, &context.tracing_job.start_time);
    ASSERT(!ret);

    context.tracing_job.buf_len = TRACING_SAMPLE;
    ASSERT(context.tracing_job.buf_len % 2 == 0);
    context.tracing_job.buf = malloc(sizeof(struct tracing_item) * context.tracing_job.buf_len);
    ASSERT(context.tracing_job.buf);

    context.tracing_job.head = context.tracing_job.buf_len;
    context.tracing_job.tail = 0;
    context.tracing_job.trace_done = 0;
    context.tracing_job.trace_synced = 0;
    ASSERT((context.tracing_job.head - context.tracing_job.tail) == context.tracing_job.buf_len);
    ret = create_job(&context.ring, &context.tracing_job.op, BACKGROUND_TRACING_MS, 0, background_tracing);
    ASSERT(!ret);
    ASSERT(context.tracing_job.op.inner.callback == background_tracing);

    LOG("created status job: %p\n", background_tracing);
}

void background_status_init()
{
    int ret;

    ret = clock_gettime(CLOCK_REALTIME, &context.status_job.last_time);
    ASSERT(!ret);

    ret = create_job(&context.ring, &context.status_job.op, BACKGROUND_STATUS_MS, 0, background_status);
    ASSERT(!ret);
    ASSERT(context.status_job.op.inner.callback == background_status);

    LOG("created status job: %p\n", background_status);
}

int create_job(struct io_uring *ring, struct op_job *op, unsigned long nsec, unsigned long sec, op_callback_t callback)
{
    struct io_uring_sqe *sqe;
    sqe = io_prepare_sqe(ring, &op->inner, callback);
    if (!sqe)
        return 1;

    op->ts.tv_nsec = nsec;
    op->ts.tv_sec = sec;
    io_uring_prep_timeout(sqe, &op->ts, 0, 0);
    LOG("created job for: %p\n", callback);
    return 0;
}

int resend_job(struct io_uring *ring, struct op_job *op)
{
    struct io_uring_sqe *sqe;
    sqe = io_prepare_sqe(ring, &op->inner, op->inner.callback);
    if (!sqe)
        return 1;

    io_uring_prep_timeout(sqe, &op->ts, 0, 0);
    LOG("resend job for: %p\n", op->inner.callback);
    return 0;
}

void stats_bucket_init(struct stats_bucket *bucket, __u32 len)
{
    bucket->acc_val = 0;
    bucket->idx = 0;
    bucket->acc_time = 0;
    bucket->len = len;
    bucket->buf = (struct stats_bucket_item *)calloc(len, sizeof(struct stats_bucket_item));
    ASSERT(bucket->buf);
    // TODO: check calloc error
}

void stats_bucket_add(struct stats_bucket *bucket)
{
    bucket->acc_val++;
    bucket->buf[bucket->idx % bucket->len].val++;
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