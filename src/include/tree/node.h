#ifndef NODE_H
#define NODE_H

#define NODE_SIZE (1 << 12)

#include <linux/types.h>
#include "tree/cell.h"

enum node_flag
{
    BTREE_NODE_FLAGS_LEAF = 1 << 0,
    BTREE_NODE_FLAGS_ROOT = 1 << 1,
};

struct node
{
    __u64 next_overflow_pid;
    __u64 last_overflow_pid;
    __u64 rightmost_pid;
    __u64 parent_pid;
    __u32 size;
    __u32 cell_offset;
    __u32 tombstone_offset;
    __u32 tombstone_bytes;
    __u16 flags;
};

struct overflow_node
{
    __u64 next_overflow_pid;
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
    __u8 *key;
    __u8 *value;
    __u32 key_size;
    __u32 value_size;
};

void debug_node_cell(struct node *node, struct cell *cell);

void debug_node(struct node *node, int show_cells, int show_tombstones);

void node_cell_pointers(struct node *node, struct cell_ptr *cell_ptr, struct cell_pointers *pointers);

struct node *node_parent(struct node *node);

void node_set_parent(struct node *node, struct node *child);

void node_set_root(struct node *node);

void node_unset_root(struct node *node);

void node_set_rightmost_child(struct node *node, struct node *child);

int node_delete_key(struct node *node, __u8 *key, __u32 key_size);

void node_init(struct node *node, __u32 flags);

int node_bin_search(struct node *node, __u8 *key, __u32 key_size, __u32 *idx);

int node_insert_nonfull(struct node *node, __u32 idx, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size);

int key_compare_cell(struct cell *cell, __u8 *key, __u32 key_size);

int __key_compare(__u8 *cell_key, __u32 cell_key_size, __u8 *key, __u32 key_size);

int key_compare(struct node *node, struct cell_ptr *cell_p, __u8 *key, __u32 key_size);

struct node *internal_node_split(struct node *node, __u32 partition_idx);

struct node *leaf_node_split(struct node *node, __u32 partition_idx);

__u32 node_partition_idx(struct node *node);

struct cell_ptr *node_get_cell(struct node *node, __u8 *key, __u32 key_size);

struct cell_ptr *node_cells(struct node *node);

struct cell_ptr *node_get_cell_ptr(struct node *node, __u32 idx);

struct cell *node_cell_from_offset(struct node *node, __u32 offset);

struct cell *node_cell_from_ptr(struct node *node, struct cell_ptr *cell_ptr);

struct cell *node_cell_from_idx(struct node *node, __u32 idx);

__u32 offset_from_cell(struct node *node, struct cell *cell);

int node_is_full(struct node *node, __u32 key_size, __u32 value_size);

__u32 node_get_free_offset(struct node *node, __u32 key_size, __u32 value_size);

int node_is_leaf(struct node *node);

int node_is_root(struct node *node);

struct node *btree_split_node(struct node *node);

void node_tuple_set_tombstone(struct node *node, __u32 idx);

void node_insert_leaf_cell(struct node *node, __u32 offset, __u32 idx, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size);

void node_insert_internal_cell(struct node *node, __u32 offset, __u32 idx, __u8 *key, __u32 key_size, struct node *child);

void node_write_leaf_cell(struct node *node, struct cell_ptr *cell_ptr, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size, __u16 flags);

void node_write_internal_cell(struct node *node, struct cell_ptr *cell_ptr, __u8 *key, __u32 key_size, struct node *child, __u16 flags);

#endif