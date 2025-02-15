#ifndef BTREE_H
#define BTREE_H

#include <linux/types.h>

struct btree
{
    struct node *root;
    __u32 count;
};

int btree_init(struct btree *btree);

struct cell_ptr *btree_search(struct btree *btree, __u8 *key, __u32 key_size);

int btree_insert_traverse(struct btree *btree, __u32 *ret_idx, struct node **ret_node, __u8 *key, __u32 key_size, __u32 value_size);

int btree_insert(struct btree *btree, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size);

struct node *btree_node_alloc(void);

#endif
