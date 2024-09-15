#ifndef UTILS_H
#define UTILS_H

#ifdef ASSERTION
#define ASSERT(x) assert(x)
#else
#define ASSERT(x)
#endif

#ifdef DEBUG
#define LOG(format, ...) printf(format, __VA_ARGS__)
#else
#define LOG(format, ...)
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