#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include "utils.h"
#include "cbuf.h"

void cbuf_init(struct cbuf *cbuf, __u32 item_size, __u32 len)
{
    __u64 sz = PAGE_ALIGN(len * item_size);
    cbuf->item_size = item_size;
    cbuf->len = len;
    cbuf->head = 0;
    cbuf->tail = 0;

    int fd = memfd_create("test_cbuf", 0);
    ASSERT(fd >= 0);
    ftruncate(fd, sz);

    cbuf->buf = mmap(NULL, 2 * sz, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(cbuf->buf);

    void *ret;

    ret = mmap(cbuf->buf, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    ASSERT(cbuf->buf == ret);
    ret = mmap(cbuf->buf + sz, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    ASSERT(cbuf->buf + sz == ret);
}

int cbuf_is_full(struct cbuf *cbuf)
{
    return cbuf_free_count(cbuf) == 0;
}

int cbuf_is_empty(struct cbuf *cbuf)
{
    return cbuf->tail == cbuf->head;
}

__u32 cbuf_free_count(struct cbuf *cbuf)
{
    return cbuf->len - (cbuf->tail - cbuf->head);
}

__u32 cbuf_used_count(struct cbuf *cbuf)
{
    return cbuf->tail - cbuf->head;
}

void *cbuf_idx(struct cbuf *cbuf, __u32 index)
{
    return &cbuf->buf[index % (cbuf->len - 1)];
}

__u32 cbuf_put(struct cbuf *cbuf, void **buf)
{
    void *start = cbuf_idx(cbuf, cbuf->tail);
    __u32 free = cbuf_free_count(cbuf);

    *buf = start;
    return free;
}

void cbuf_advance_tail(struct cbuf *cbuf, __u32 len)
{
    cbuf->tail += len;
}

void cbuf_advance_head(struct cbuf *cbuf, __u32 len)
{
    cbuf->head += len;
}

__u32 cbuf_get(struct cbuf *cbuf, void **out)
{
    __u32 used = cbuf_used_count(cbuf);

    void *start = cbuf_idx(cbuf, cbuf->head);
    *out = start;

    return used;
}