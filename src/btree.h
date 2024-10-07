#ifndef BTREE_H
#define BTREE_H

#include <linux/types.h>

#define NODE_SIZE (1 << 15)

struct btree
{
    struct btree_page_hdr *root;
};

enum btree_page_flag
{
    BTREE_PAGE_FLAGS_LEAF = 1 << 0,
};

struct btree_page_hdr
{
    __s64 next_overflow_pid;
    __s64 last_overflow_pid;
    __s64 rightmost_pid;
    __u32 size;
    __u32 cell_offset;
    __u32 tombstone_offset;
    __u32 tombstone_bytes;
    __u16 flags;
};

struct btree_overflow_page_hdr
{
    __s64 next_overflow_pid;
    __u32 next_free_offset;
    __u32 tombstone_offset;
    __u16 flags;
};

struct btree_cell_tombstone
{
    __u32 next_tombstone_offset;
    __u32 size;
};

struct btree_cell_ptr
{
    __u32 offset;
    __u32 key_prefix;
};

struct __attribute__((packed)) btree_internal_cell
{
    __s64 pid;
    __u32 key_size;
    __u8 key[];
};

struct __attribute__((packed)) btree_leaf_cell
{
    __u32 data_cell_offset;
    __u32 key_size;
    __u32 value_size;
    __u16 flags;
    __u8 content[];
};

struct btree_overflowed_cell_suffix
{
    __u64 overflow_pid;
    __u32 offset;
};

struct btree_cell_pointers
{
    void *key;
    void *value;
    __u32 key_size;
    __u32 value_size;
};

void btree_cell_pointers_get(struct btree_page_hdr *hdr, struct btree_cell_ptr *cell_ptr, struct btree_cell_pointers *pointers);

struct btree_page_hdr *btree_node_alloc(void);

int btree_node_bin_search(struct btree_page_hdr *hdr, void *key, __u32 key_len, __u32 *idx);

int btree_leaf_node_insert(struct btree_page_hdr *hdr, void *key, __u32 key_len, void *data, __u32 data_len);

struct btree_cell_ptr *btree_node_get(struct btree_page_hdr *hdr, void *key, __u32 key_len);

struct btree_cell_ptr *btree_cells(struct btree_page_hdr *hdr);

struct btree_cell_ptr *btree_get_cell_ptr(struct btree_page_hdr *hdr, __u32 idx);

void *btree_cell_get(struct btree_page_hdr *hdr, struct btree_cell_ptr *cell_ptr);

struct btree_cell_ptr *btree_search(struct btree *btree, void *key, __u32 key_len);

int btree_insert(struct btree *btree, void *key, __u32 key_len, void *data, __u32 data_len);

#endif
