#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "tree/btree.h"
#include "tree/node.h"

int btree_init(struct btree *btree)
{
    struct node *node = btree_node_alloc();
    if (!node)
        return -1;

    btree->root = node;
    node_init(btree->root, BTREE_NODE_FLAGS_ROOT | BTREE_NODE_FLAGS_LEAF);
    return 0;
}

struct cell_ptr *btree_search(struct btree *btree, void *key, __u32 key_size)
{
    struct cell_ptr *tuple_hdr = node_get_cell(btree->root, key, key_size);
    if (!tuple_hdr)
        return NULL;

    return NULL;
}

int btree_insert(struct btree *btree, void *key, __u32 key_size, void *value, __u32 value_size)
{
    int ret;

    ret = node_insert(btree->root, key, key_size, value, value_size);

    return ret;
}

struct node *btree_node_alloc(void)
{
    void *node = malloc(NODE_SIZE);
    if (!node)
        return NULL;

    struct node *hdr = (struct node *)node;

    return hdr;
}