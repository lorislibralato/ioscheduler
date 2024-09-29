#ifndef CBUF_H
#define CBUF_H

#include <linux/types.h>

struct cbuf
{
    void *buf;
    int fd;
    __u32 len;
    __u32 item_size;
    __u32 head;
    __u32 tail;
};

void cbuf_init(struct cbuf *cbuf, __u32 item_size, __u32 len);
__u32 cbuf_put(struct cbuf *cbuf, void **payload);
__u32 cbuf_get(struct cbuf *cbuf, void **out);
int cbuf_is_full(struct cbuf *cbuf);
int cbuf_is_empty(struct cbuf *cbuf);
__u32 cbuf_free_count(struct cbuf *cbuf);
void cbuf_advance_tail(struct cbuf *cbuf, __u32 len);
void cbuf_advance_head(struct cbuf *cbuf, __u32 len);

#endif