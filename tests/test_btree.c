#define ASSERTION
#define DEBUG
#include <string.h>
#include <stdio.h>
#include "../src/include/utils.h"
#include "../src/include/tree/btree.h"
#include "../src/include/tree/node.h"
#include "../src/include/tree/cell.h"

#define TUPLE_COUNT (17)

void validate_order(struct node *node, struct cell *lower_limit, struct cell *upper_limit)
{
    if (node_is_leaf(node))
    {
        for (__u32 i = 1; i < node->size; i++)
        {
            struct cell *cell_prev = node_cell_from_idx(node, i - 1);
            struct cell *cell = node_cell_from_idx(node, i);
            ASSERT(key_compare_cell(cell_prev, cell_get_key(cell), cell->key_size) <= 0);
        }

        for (__u32 i = 0; i < node->size; i++)
        {
            struct cell *cell = node_cell_from_idx(node, i);
            if (lower_limit)
                ASSERT(key_compare_cell(cell, cell_get_key(lower_limit), lower_limit->key_size) >= 0);

            if (upper_limit)
                ASSERT(key_compare_cell(cell, cell_get_key(upper_limit), upper_limit->key_size) < 0);
        }
    }
    else
    {
        for (__u32 i = 1; i < node->size; i++)
        {
            struct cell *cell_prev = node_cell_from_idx(node, i - 1);
            struct cell *cell = node_cell_from_idx(node, i);

            ASSERT(key_compare_cell(cell_prev, cell_get_key(cell), cell->key_size) <= 0);
        }

        for (__u32 i = 0; i < node->size; i++)
        {
            struct cell *cell = node_cell_from_idx(node, i);
            if (lower_limit)
                ASSERT(key_compare_cell(cell, cell_get_key(lower_limit), lower_limit->key_size) >= 0);

            if (upper_limit)
                ASSERT(key_compare_cell(cell, cell_get_key(upper_limit), upper_limit->key_size) < 0);
        }

        // first cell
        if (node->size)
            validate_order(internal_cell_child(node_cell_from_idx(node, 0)), lower_limit, node->size > 1 ? node_cell_from_idx(node, 1) : upper_limit);

        // inner cells
        for (__u32 i = 1; i < node->size - 1; i++)
            validate_order(internal_cell_child(node_cell_from_idx(node, i)), node_cell_from_idx(node, i - 1), node_cell_from_idx(node, i + 1));

        // last cell
        if (node->size > 1)
            validate_order(internal_cell_child(node_cell_from_idx(node, node->size - 1)), node_cell_from_idx(node, node->size - 2), upper_limit);

        // rightmost child
        if (node->rightmost_pid)
            validate_order((struct node *)node->rightmost_pid, node->size ? node_cell_from_idx(node, node->size - 1) : lower_limit, upper_limit);
    }
}

void print_tree(struct node *node, __u32 level, int show_cells, int show_tombstones)
{
    if (node_is_leaf(node))
    {
        LOG("level: %u\n", level);
        debug_node(node, show_cells, show_tombstones);
    }
    else
    {
        LOG("level: %u\n", level);
        debug_node(node, show_cells, show_tombstones);
        for (__u32 i = 0; i < node->size; i++)
        {
            print_tree(internal_cell_child(node_cell_from_idx(node, i)), level + 1, show_cells, show_tombstones);
        }

        if (node->rightmost_pid)
            print_tree((struct node *)node->rightmost_pid, level + 1, show_cells, show_tombstones);
    }
}

void count_node(struct node *node, __u32 *leaf, __u32 *internal, __u32 *tuple)
{
    if (node_is_leaf(node))
    {
        *tuple = *tuple + node->size;
        *leaf = *leaf + 1;
    }
    else
    {
        *internal = *internal + 1;
        for (__u32 i = 0; i < node->size; i++)
            count_node(internal_cell_child(node_cell_from_idx(node, i)), leaf, internal, tuple);

        if (node->rightmost_pid)
            count_node((struct node *)node->rightmost_pid, leaf, internal, tuple);
    }
}

int main()
{
    struct btree btree;
    int ret;

    ret = btree_init(&btree);
    ASSERT(!ret);

    __u8 key[512];
    __u8 value[512];

    for (__s32 i = 0; i < TUPLE_COUNT; i++)
    {
        snprintf((char *)key, ARRAY_LEN(key), "key-%.4x", i);
        snprintf((char *)value, ARRAY_LEN(value), "value-%.4x", i);

        // LOG("SET(%d) %.*s = %.*s\n", i, (int)ARRAY_LEN(key), (char *)key, (int)ARRAY_LEN(value), (char *)value);

        ret = btree_insert(&btree, key, ARRAY_LEN(key), value, ARRAY_LEN(value));
        ASSERT(!ret);
    }

    // print_tree(btree.root, 0, 1, 0);
    
    __u32 leaf = 0, internal = 0, tuple = 0;
    count_node(btree.root, &leaf, &internal, &tuple);
    LOG("LEAF: %d, INTERNAL: %d\n", leaf, internal);
    ASSERT(tuple == btree.count);
    
    validate_order(btree.root, NULL, NULL);

    LOG("TEST (%s): ok\n", __FILE__);
}
