#define ASSERTION
#define DEBUG
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../src/include/utils.h"
#include "../src/include/tree/btree.h"
#include "../src/include/tree/node.h"
#include "../src/include/tree/cell.h"

#define TUPLE_COUNT (100 * 1024 * 1024)

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

void tree_info(struct node *node, __u32 *leaf, __u32 *internal, __u32 *tuple)
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
            tree_info(internal_cell_child(node_cell_from_idx(node, i)), leaf, internal, tuple);

        if (node->rightmost_pid)
            tree_info((struct node *)node->rightmost_pid, leaf, internal, tuple);
    }
}

int main()
{
    struct btree btree;
    int err;

    err = btree_init(&btree);
    ASSERT(!err);

    __u8 key[16];
    __u8 value[64];

    char *key_prefix = "key-0x";
    char *val_prefix = "value-0x";

    // LOG("%lld >= %lld\n", ((__s64)ARRAY_LEN(key) - (__s64)strlen(key_prefix)), ((__s64)ceil(log(TUPLE_COUNT) / log(16) + 2)));
    // LOG("%lld >= %lld\n", ((__s64)ARRAY_LEN(value) - (__s64)strlen(val_prefix)), ((__s64)ceil(log(TUPLE_COUNT) / log(16) + 2)));

    ASSERT(((__s64)ARRAY_LEN(key) - (__s64)strlen(key_prefix)) >= ((__s64)ceil(log(TUPLE_COUNT) / log(16) + 2)));
    ASSERT(((__s64)ARRAY_LEN(value) - (__s64)strlen(val_prefix)) >= ((__s64)ceil(log(TUPLE_COUNT) / log(16) + 2)));

    for (__u32 i = 1; i <= TUPLE_COUNT; i++)
    {
        snprintf((char *)key, ARRAY_LEN(key), "%s%.*d", key_prefix, ((int)(__s64)ceil(log(TUPLE_COUNT) / log(16) + 2)), i);
        snprintf((char *)value, ARRAY_LEN(value), "%s%.*d", val_prefix, ((int)(__s64)ceil(log(TUPLE_COUNT) / log(16) + 2)), i);

        // LOG("SET(%d) %.*s = %.*s\n", i, (int)ARRAY_LEN(key), (char *)key, (int)ARRAY_LEN(value), (char *)value);

        err = btree_insert(&btree, key, ARRAY_LEN(key), value, ARRAY_LEN(value));
        ASSERT(!err);

        // print_tree(btree.root, 0, 1, 0);
        // __u32 leaf = 0, internal = 0, tuple = 0;
        // tree_info(btree.root, &leaf, &internal, &tuple);
        // ASSERT(tuple == btree.count);
        // LOG("LEAF NODES: %d, INTERNAL NODES: %d, TUPLES: %d\n", leaf, internal, tuple);
        // validate_order(btree.root, NULL, NULL);
    }

    validate_order(btree.root, NULL, NULL);
    __u32 leaf = 0, internal = 0, tuple = 0;
    tree_info(btree.root, &leaf, &internal, &tuple);
    LOG("LEAF NODES: %d, INTERNAL NODES: %d, TUPLES: %d RAM USED: %.3fMB FOR REAL DATA: %.3fMB\n",
        leaf, internal, tuple,
        (double)NODE_SIZE * (leaf + internal) / (1024 * 1024),
        (double)tuple * (ARRAY_LEN(key) + ARRAY_LEN(value)) / (1024 * 1024));
    // print_tree(btree.root, 0, 1, 0);

    LOG("TEST (%s): ok\n", __FILE__);
}
