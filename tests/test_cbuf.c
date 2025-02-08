#define ASSERTION
#define DEBUG
#include "../src/include/utils.h"
#include "../src/include/cbuf.h"
#include <string.h>

int main()
{
    struct test_item
    {
        __u64 a;
        __u64 b;
    };
    struct cbuf cbuf;
    cbuf_init(&cbuf, sizeof(struct test_item), 4);

    struct test_item *buf;

    __u32 free_count;
    __u32 used_count;

    ASSERT(!cbuf_is_full(&cbuf));
    ASSERT(cbuf_is_empty(&cbuf));
    ASSERT(cbuf_free_count(&cbuf) == cbuf.len);

    free_count = cbuf_put(&cbuf, (void **)&buf);
    ASSERT(free_count == cbuf.len);
    buf[0].a = 1;
    buf[1].a = 2;
    ASSERT(free_count == cbuf.len);
    cbuf_advance_tail(&cbuf, 2);

    ASSERT(!cbuf_is_full(&cbuf));
    ASSERT(!cbuf_is_empty(&cbuf));
    ASSERT(cbuf_free_count(&cbuf) == cbuf.len - 2);

    used_count = cbuf_get(&cbuf, (void **)&buf);
    ASSERT(buf[0].a == 1);
    ASSERT(buf[1].a == 2);
    ASSERT(used_count == 2);
    cbuf_advance_head(&cbuf, 2);

    ASSERT(!cbuf_is_full(&cbuf));
    ASSERT(cbuf_is_empty(&cbuf));
    ASSERT(cbuf_free_count(&cbuf) == cbuf.len);

    free_count = cbuf_put(&cbuf, (void **)&buf);
    buf[0].a = 1;
    buf[1].a = 2;
    buf[2].a = 3;
    buf[3].a = 4;
    ASSERT(free_count == cbuf.len);
    cbuf_advance_tail(&cbuf, 4);

    ASSERT(cbuf_is_full(&cbuf));
    ASSERT(!cbuf_is_empty(&cbuf));
    ASSERT(cbuf_free_count(&cbuf) == 0);

    LOG("TEST (%s): ok\n", __FILE__);
}