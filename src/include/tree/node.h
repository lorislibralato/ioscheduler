#ifndef NODE_H
#define NODE_H

#define NODE_SIZE (1 << 15)

#include <linux/types.h>
#include "cell.h"

enum node_flag
{
    BTREE_NODE_FLAGS_LEAF = 1 << 0,
    BTREE_NODE_FLAGS_ROOT = 1 << 1,
};

struct node
{
    __s64 next_overflow_pid;
    __s64 last_overflow_pid;
    __s64 rightmost_pid;
    __s64 parent_pid;
    __u32 size;
    __u32 cell_offset;
    __u32 tombstone_offset;
    __u32 tombstone_bytes;
    __u16 flags;
};

struct overflow_node
{
    __s64 next_overflow_pid;
    __u32 next_free_offset;
    __u32 tombstone_offset;
    __u16 flags;
};

struct btree_overflowed_cell_suffix
{
    __u64 overflow_pid;
    __u32 offset;
};

struct cell_pointers
{
    void *key;
    void *value;
    __u32 key_size;
    __u32 value_size;
};

void node_cell_pointers(struct node *node, struct cell_ptr *cell_ptr, struct cell_pointers *pointers);

void node_init(struct node *node, __u32 flags);

int node_bin_search(struct node *node, void *key, __u32 key_size, __u32 *idx);

int node_insert(struct node *node, void *key, __u32 key_size, void *value, __u32 value_size);

struct node *internal_node_split(struct node *node, __u32 partition_idx);

struct node *leaf_node_split(struct node *node, __u32 partition_idx);

__u32 node_partition_idx(struct node *node);

struct cell_ptr *node_get_cell(struct node *node, void *key, __u32 key_size);

struct cell_ptr *node_cells(struct node *node);

struct cell_ptr *node_get_cell_ptr(struct node *node, __u32 idx);

struct cell *node_cell_from_offset(struct node *node, __u32 offset);

struct cell *node_cell_from_ptr(struct node *node, struct cell_ptr *cell_ptr);

__u32 offset_from_cell(struct node *node, void *cell);

__u32 node_get_free_offset(struct node *node, __u32 key_size, __u32 value_size);

int is_node_leaf(struct node *node);

struct node *btree_split_node(struct node *node);

void node_tuple_set_tombstone(struct node *node, __u32 idx);

void node_insert_leaf_cell(struct node *node, __u32 offset, __u32 idx, void *key, __u32 key_size, void *value, __u32 value_size);

void node_insert_internal_cell(struct node *node, __u32 offset, __u32 idx, void *key, __u32 key_size, struct node *child);

void node_write_leaf_cell(struct node *node, struct cell_ptr *cell_ptr, void *key, __u32 key_size, void *value, __u32 value_size, __u16 flags);

void node_write_internal_cell(struct node *node, struct cell_ptr *cell_ptr, void *key, __u32 key_size, struct node *child, __u16 flags);

#endif