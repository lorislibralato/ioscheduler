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
#define LOG(fmt, ...) printf(fmt, __VA_ARGS__)
#else
void no_effect_printf(const char *__restrict __fmt, ...)
{
  (void)__fmt;
}
#define LOG(fmt, ...) no_effect_printf(fmt, __VA_ARGS__)
#endif

#define TIME_US(x) ((__u64)(x) * 1000)
#define TIME_MS(x) (TIME_US((x) * 1000))
#define TIME_S(x) (TIME_MS((x) * 1000))

#define max(a, b) \
  ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a, b) \
  ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#endif
