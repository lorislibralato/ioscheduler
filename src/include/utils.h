#ifndef UTILS_H
#define UTILS_H

#ifdef ASSERTION
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x) (void)(x)
#endif

#ifdef DEBUG
#include <stdio.h>
#define LOG(...) printf(__VA_ARGS__)
#else
void no_effect_printf(const char *__restrict __fmt, ...)
{
  (void)__fmt;
}
#define LOG(...) no_effect_printf(__VA_ARGS__)
#endif

#define BYTE_KB(x) ((__u64)(x) << 10)
#define BYTE_MB(x) (BYTE_KB((x) << 10))
#define BYTE_GB(x) (BYTE_MB((x) << 10))

#define TIME_US(x) ((__u64)(x) * 1000)
#define TIME_MS(x) (TIME_US((x) * 1000))
#define TIME_S(x) (TIME_MS((x) * 1000))

#define PAGE_SZ_BITS (12)
#define PAGE_SZ (1 << PAGE_SZ_BITS)
#define PAGE_ALIGN(x) ALIGN(x, PAGE_SZ)

#define ALIGN(x, a) __ALIGN_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))

#define max(a, b) \
  ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a, b) \
  ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define container_of(ptr, type, member) ({               \
   const typeof(((type *)0)->member) * __mptr = (ptr);   \
   (type *)((char *)__mptr - offsetof(type, member)); })

#define container_of_op_field(name, type, base, field) __typeof__(type) *name = container_of(base, __typeof__(type), field)

#define container_of_op(name, type, base) container_of_op_field(name, type, base, inner)

#define container_of_job_op(name, type, base) container_of_op_field(name, type, base, inner.inner)

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

#endif
