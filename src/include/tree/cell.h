#ifndef CELL_H
#define CELL_H

#include <linux/types.h>

struct cell_ptr
{
    __u32 offset;
};

struct __attribute__((packed)) cell
{
    union
    {
        struct
        {
            // leaf
            __u32 flags;
            __u32 value_size;
        };
        struct
        {
            // internal
            __s64 pid;
        };
        struct
        {
            // tombstone
            __u32 tombstone_size;
            __u32 next_off;
        };
    };
    __u32 key_size;
    __u32 total_size;
    __u8 content[];
};

#endif