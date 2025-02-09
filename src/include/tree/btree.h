#ifndef BTREE_H
#define BTREE_H

#include <linux/types.h>

struct btree
{
    struct node *root;
};

int btree_init(struct btree *btree);

struct cell_ptr *btree_search(struct btree *btree, void *key, __u32 key_size);

int btree_insert(struct btree *btree, void *key, __u32 key_size, void *value, __u32 value_size);

struct node *btree_node_alloc(void);

#endif
