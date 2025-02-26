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
            __u64 pid;
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

void debug_cell(struct cell *cell);

__u8 *leaf_cell_get_value(struct cell *cell);

__u8 *cell_get_key(struct cell *cell);

struct node* internal_cell_child(struct cell *cell);

#endif