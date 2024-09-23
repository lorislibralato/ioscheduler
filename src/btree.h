#ifndef BTREE_H
#define BTREE_H

#include <linux/types.h>

#define NODE_SIZE (1 << 15)

struct btree
{
    struct btree_node_hdr *root;
};

struct __attribute__((aligned(64))) btree_node_hdr
{
    __u16 len;
    __u16 tuple_offset_limit;
    __u16 tombstone_offset;
};

struct btree_tuple_tombstone
{
    __u16 next_tombstone_offset;
    __u16 tuple_hdr_index;
};

struct btree_tuple_hdr
{
    __u16 key_len;
    __u16 data_len;
    __u16 offset;
    __u8 key_prefix;
    __u8 flags;
};

struct btree_tuple_pointers
{
    __u8 *key;
    __u8 *data;
    __u16 key_len;
    __u16 data_len;
};

struct btree_node_hdr *btree_node_alloc(void);

int btree_node_bin_search(struct btree_node_hdr *hdr, void *key, __u16 key_len, __u16 *idx);

int btree_node_insert(struct btree_node_hdr *hdr, void *key, __u16 key_len, void *data, __u16 data_len);

struct btree_tuple_hdr *btree_node_get(struct btree_node_hdr *hdr, void *key, __u16 key_len);

void btree_tuple_get_pointers(struct btree_node_hdr *hdr, struct btree_tuple_hdr *tuple_hdr, struct btree_tuple_pointers *out);

struct btree_tuple_hdr *btree_tuple_get_hdrs(struct btree_node_hdr *hdr);

struct btree_tuple_hdr *btree_tuple_get_hdr(struct btree_node_hdr *hdr, __u16 idx);

__u8 *btree_tuple_get(struct btree_node_hdr *hdr, struct btree_tuple_hdr *tuple_hdr);

#endif
