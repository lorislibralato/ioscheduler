#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "tree/btree.h"
#include "tree/cell.h"
#include "tree/node.h"

int btree_init(struct btree *btree)
{
    struct node *node = btree_node_alloc();
    if (!node)
        return -1;

    btree->count = 0;
    btree->root = node;
    node_init(btree->root, BTREE_NODE_FLAGS_ROOT | BTREE_NODE_FLAGS_LEAF);
    return 0;
}

struct cell_ptr *btree_search(struct btree *btree, __u8 *key, __u32 key_size)
{
    struct cell_ptr *tuple_hdr = node_get_cell(btree->root, key, key_size);
    if (!tuple_hdr)
        return NULL;

    return NULL;
}

int btree_insert(struct btree *btree, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size)
{
    int ret;
    __u32 ret_idx;
    struct node *ret_node;

    // LOG("travering for: %.*s\n", key_size, key);
    ret = btree_insert_traverse(btree, &ret_idx, &ret_node, key, key_size, value_size);
    if (ret)
        return -1;

    // LOG("inserting: %.*s\n", key_size, key);
    ret = node_insert_nonfull(ret_node, ret_idx, key, key_size, value, value_size);
    if (ret)
        return -1;

    btree->count += 1;

    return 0;
}


int btree_insert_traverse(struct btree *btree, __u32 *ret_idx, struct node **ret_node, __u8 *key, __u32 key_size, __u32 value_size)
{
    struct node *new_node, *parent, *node = btree->root;
    int ret, is_full;
    __u32 idx;
    __u32 partition_idx;

    // check if root is full, if so split the root
    is_full = node_is_full(node, key_size, node_is_leaf(node) ? value_size : 0);
    if (is_full)
    {
        // LOG("\n\nSPLITTING ROOT\n");
        // debug_node(node);

        struct node *new_root;
        __u32 root_offset;

        // alloc new root node
        new_root = btree_node_alloc();
        ASSERT(new_root);
        node_init(new_root, BTREE_NODE_FLAGS_ROOT);

        // check tuple index to use for partition
        partition_idx = node_partition_idx(node);
        // split the node and get the new neighbor node
        if (node_is_leaf(node))
            new_node = leaf_node_split(node, partition_idx);
        else
            new_node = internal_node_split(node, partition_idx);

        // insert the partition key cell in the new root node and index 0
        struct cell *partitioned_cell = node_cell_from_idx(new_node, 0);

        // get offset of free space in new root node
        root_offset = node_get_free_offset(new_root, partitioned_cell->key_size, 0);
        ASSERT(root_offset > 0);

        node_insert_internal_cell(new_root, root_offset, 0, cell_get_key(partitioned_cell), partitioned_cell->key_size, node);
        node_set_rightmost_child(new_root, new_node);

        // prepare the new root node to be the parent of the old root node
        btree->root = new_root;
        node_set_parent(new_node, new_root);
        node_set_parent(node, new_root);
        node_unset_root(node);

        // LOG("\n\nROOT\n");
        // debug_node(new_root);
        // LOG("\n\nLEFT CHILD\n");
        // debug_node(node);
        // LOG("\n\nRIGHT CHILD\n");
        // debug_node(new_node);

        if (key_compare_cell(node_cell_from_idx(new_node, 0), key, key_size) < 0)
        {
            node = new_node;
            is_full = node_is_full(node, key_size, node_is_leaf(node) ? value_size : 0);
        }
    }

    do
    {
        // LOG("NODE IN WHILE LOOP %p\n", node);
        // if there is no free space split the node
        if (is_full)
        {
            // LOG("\n\nSPLITTING NODE (NON ROOT)\n");
            // check tuple index to use for partition
            partition_idx = node_partition_idx(node);
            // LOG("partition_idx: %d\n", partition_idx);

            if (node_is_leaf(node))
                new_node = leaf_node_split(node, partition_idx);
            else
                new_node = internal_node_split(node, partition_idx);

            parent = node_parent(node);
            node_set_parent(new_node, parent);

            // insert the partition key in the parent internal node and link the new node
            struct cell *partitioned_cell = node_cell_from_idx(new_node, 0);
            ret = node_bin_search(parent, cell_get_key(partitioned_cell), partitioned_cell->key_size, &idx);
            ASSERT(idx >= 0);

            if (idx < parent->size)
                node_cell_from_idx(parent, idx)->pid = (__u64)new_node;
            else
                node_set_rightmost_child(parent, new_node);

            __u32 parent_offset = node_get_free_offset(parent, partitioned_cell->key_size, 0);
            ASSERT(parent_offset > 0);
            node_insert_internal_cell(parent, parent_offset, idx, cell_get_key(partitioned_cell), partitioned_cell->key_size, node);

            // LOG("\nPARENT\n");
            // debug_node(parent, 1, 0);

            // LOG("\nLEFT CHILD\n");
            // debug_node(node, 1, 0);

            // LOG("\nRIGHT CHILD\n");
            // debug_node(new_node, 1, 0);

            if (key_compare_cell(partitioned_cell, key, key_size) < 0)
                node = new_node;
        }

        // follow the child node
        ret = node_bin_search(node, key, key_size, &idx);
        ASSERT(idx >= 0);

        LOG("following route with idx: %d\n", idx);
        // LOG("\n\nFOLLOWED CHILD\n");
        // debug_node(node, 0, 0);

        if (node_is_leaf(node))
            break;

        // LOG("\n\n");
        // debug_node(node, 1, 0);
        // TODO: resolve pid to page in memory
        if (idx < node->size)
        {
            struct cell *cell = node_cell_from_idx(node, idx);
            node = internal_cell_child(cell);
        }
        else
        {
            debug_node(node, 1, 0);
            ASSERT(node->rightmost_pid);
            node = (struct node *)node->rightmost_pid;
        }

        // debug_node(node, 1, 0);
        // LOG("\n\nFollowing node %p\n", node);
        is_full = node_is_full(node, key_size, node_is_leaf(node) ? value_size : 0);
    } while (1);

    // LOG("\n\nAR<RIVED CHILD\n");
    // debug_node(node);

    *ret_node = node;
    *ret_idx = idx;
    // 1 if the key is found, 0 if the key is not found
    return ret;
}

struct node *btree_node_alloc(void)
{
    void *mem = malloc(NODE_SIZE);
    if (!mem)
        return NULL;

    struct node *node = (struct node *)mem;
    return node;
}