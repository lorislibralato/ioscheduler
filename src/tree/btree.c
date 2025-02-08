#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "tree/btree.h"
#include "tree/node.h"

struct cell_ptr *btree_search(struct btree *btree, void *key, __u32 key_size)
{
    struct cell_ptr *tuple_hdr = btree_node_get(btree->root, key, key_size);
    if (!tuple_hdr)
        return NULL;

    return NULL;
}

int btree_insert(struct btree *btree, void *key, __u32 key_size, void *value, __u32 value_size)
{
    (void)btree;
    (void)key;
    (void)key_size;
    (void)value;
    (void)value_size;
    
    return 0;
}

struct node *btree_node_alloc(void)
{
    void *node = malloc(NODE_SIZE);
    ASSERT(node);

    struct node *hdr = (struct node *)node;

    return hdr;
}