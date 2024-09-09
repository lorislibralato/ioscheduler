#include "scheduler.h"

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
            printf("cqe_res = %d | func = %p\n", cqe->res, op->callback);

            if (cqe->res == -ETIME)
                assert(op->callback == background_flusher ||
                       op->callback == background_status ||
                       op->callback == background_writer);
            assert(op);
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
    io_uring_sqe_set_data(sqe, (void *)op);

    return sqe;
}

int page_written(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_page_write *op = (struct op_page_write *)base_op;
    printf("op: %p\n", op);
    assert(op != NULL);
    // TODO: assert(op->page_id == page_id_check_order);
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

    context.writer_job.written++;
    context.writer_job.inflight--;
    stats_bucket_add(&background_write_stats);

    return 0;
}

int page_read(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_page_read *op = (struct op_page_read *)base_op;
    assert(op);
    // TODO: implement

    return 0;
}

int file_synced(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct op_file_synced *op = (struct op_file_synced *)base_op;
    assert(op);

    context.writer_job.written = 0;

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
    assert(op);
    __u32 i = 0;

    struct io_uring_sqe *sqe;
    struct op_page_read *op_page_read = malloc(sizeof(struct op_page_read));
    assert(op_page_read);
    op_page_read->page_id = op->page_id + i;

    sqe = io_prepare_sqe(&context.ring, &op_page_read->inner, page_read);
    assert(sqe);
    // io_uring_prep_read(sqe, op->fd, op->buf, BUF_SIZE, (__u64)(BUF_SIZE) * ((__u64)op->page_id + i));

    return 0;
}

int background_writer(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct writer_job *op = (struct writer_job *)base_op;
    assert(op);

    struct io_uring_sqe *sqe;
    struct op_page_write *op_page_write;

    if (op->inflight <= 16 && op->batch_size < (ENTRIES >> 1))
    {
        op->batch_size++;
    }
    else if (op->batch_size > 0 && op->inflight >= 64)
    {
        op->batch_size--;
    }

    __u64 i = 0;
    for (; i < op->batch_size; i++)
    {
        op_page_write = malloc(sizeof(struct op_page_write));
        assert(op_page_write);
        op_page_write->page_id = op->page_id + i;
        op_page_write->next = NULL;
        op_page_write->user_fsync_callback = NULL;
        sqe = io_prepare_sqe(&context.ring, &op_page_write->inner, page_written);
        assert(sqe);
        io_uring_prep_write(sqe, op->fd, op->buf, BUF_SIZE, (__u64)(BUF_SIZE) * ((__u64)op_page_write->page_id));

        op->inflight++;
    }
    op->page_id += op->batch_size;

    int ret = resend_job(&context.ring, &op->op);
    assert(!ret);

    return 0;
}

int background_flusher(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;
    struct flusher_job *op = (struct flusher_job *)base_op;
    assert(op);

    struct io_uring_sqe *sqe;

    if (context.writer_job.written && !op->running)
    {
        sqe = io_prepare_sqe(&context.ring, (struct op *)&op->op_fsync, file_synced);
        assert(sqe);
        io_uring_prep_fsync(sqe, op->fd, 0);
        op->running = 1;
    }

    int ret = resend_job(&context.ring, &op->op);
    assert(!ret);

    return 0;
}

int background_status(struct op *base_op, struct io_uring_cqe *cqe)
{
    (void)cqe;

    struct status_job *op = (struct status_job *)base_op;
    assert(op);

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    unsigned long long elapsed_ms = ((now.tv_sec - op->last_time.tv_sec) * 1000 + (now.tv_nsec - op->last_time.tv_nsec) / 1000 / 1000);
    unsigned long long drift_ns = ((now.tv_sec - op->last_time.tv_sec - op->op.ts.tv_sec) * 1000 * 1000 * 1000 + (now.tv_nsec - op->last_time.tv_nsec - op->op.ts.tv_nsec));
    (void)drift_ns;

    unsigned long long mbs_speed = (BUF_SIZE * background_write_stats.acc) / (1 << 20);

    printf("\33[2K\r\tinflight: %u | iops: %u | mb/s: %llu | batch: %u | elapsed: %llu ms",
           context.writer_job.inflight, background_write_stats.acc, mbs_speed, context.writer_job.batch_size, elapsed_ms);
    fflush(stdout);

    stats_bucket_move(&background_write_stats);
    op->last_time = now;

    int ret = resend_job(&context.ring, &op->op);
    assert(!ret);
    return 0;
}

void background_writer_init(int fd)
{
    static char buf[BUF_SIZE] = {'a'};
    int ret;

    context.writer_job.buf = buf;
    context.writer_job.fd = fd;
    context.writer_job.page_id = 0;
    context.writer_job.inflight = 0;
    context.writer_job.written = 0;
    context.writer_job.batch_size = 1;
    ret = create_job(&context.ring, &context.writer_job.op, WRITE_TIMEOUT_MS, 0, background_writer);
    assert(!ret);
    assert(context.writer_job.op.inner.callback == background_writer);
    printf("created writed job: %p\n", background_writer);
}

void background_reader_init(int fd)
{
    int ret;
    context.reader_job.fd = fd;
    context.reader_job.page_id = 0;
    ret = create_job(&context.ring, &context.reader_job.op, WRITE_TIMEOUT_MS, 0, background_reader);
    assert(!ret);
}

void background_flusher_init(int fd)
{
    int ret;
    context.flusher_job.fd = fd;
    context.flusher_job.running = 0;
    context.flusher_job.write_list.head = context.flusher_job.write_list.tail = NULL;
    ret = create_job(&context.ring, &context.flusher_job.op, BACKGROUND_FLUSH_MS, 0, background_flusher);
    assert(!ret);
    assert(context.flusher_job.op.inner.callback == background_flusher);

    printf("created flusher job: %p\n", background_flusher);
}

void background_status_init()
{
    int ret;

    clock_gettime(CLOCK_REALTIME, &context.status_job.last_time);
    ret = create_job(&context.ring, &context.status_job.op, BACKGROUND_STATUS_MS, 0, background_status);
    assert(!ret);
    assert(context.status_job.op.inner.callback == background_status);

    printf("created status job: %p\n", background_status);
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
    printf("created job for: %p\n", callback);
    return 0;
}

int resend_job(struct io_uring *ring, struct op_job *op)
{
    struct io_uring_sqe *sqe;
    sqe = io_prepare_sqe(ring, &op->inner, op->inner.callback);
    if (!sqe)
        return 1;

    io_uring_prep_timeout(sqe, &op->ts, 0, 0);
    printf("resend job for: %p\n", op->inner.callback);
    return 0;
}

void stats_bucket_init(struct stats_bucket *bucket, __u32 len)
{
    bucket->acc = 0;
    bucket->idx = 0;
    bucket->len = len;
    bucket->buf = (__u32 *)calloc(sizeof(__u32), len);
    assert(bucket->buf);
    // TODO: check calloc error
}

void stats_bucket_add(struct stats_bucket *bucket)
{
    bucket->acc++;
    bucket->buf[bucket->idx % bucket->len]++;
}

void stats_bucket_move(struct stats_bucket *bucket)
{
    unsigned int next_idx = ++bucket->idx % bucket->len;
    bucket->acc -= bucket->buf[next_idx];
    bucket->buf[next_idx] = 0;
}