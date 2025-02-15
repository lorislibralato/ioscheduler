#include <stdlib.h>
#include <string.h>
#include "utils.h"

#include "tree/node.h"
#include "tree/cell.h"
#include "tree/btree.h"

void node_init(struct node *node, __u32 flags)
{
    node->size = 0;
    node->tombstone_offset = 0;
    node->flags = 0;
    node->last_overflow_pid = 0;
    node->next_overflow_pid = 0;
    node->parent_pid = 0;
    node_set_rightmost_child(node, NULL);
    node->tombstone_bytes = 0;
    node->cell_offset = NODE_SIZE;
    node->flags = flags;
}

void debug_node_cell(struct node *node, struct cell *cell)
{
    debug_cell(cell);
    if (node)
    {

        if (node_is_leaf(node))
        {
            LOG("flags: %u\n", cell->flags);
            LOG("value_size: %u\n", cell->value_size);
            LOG("value: %.*s\n", cell->value_size, leaf_cell_get_value(cell));
        }
        else
        {
            LOG("pid: %p\n", (struct node *)cell->pid);
        }
        LOG("offset: %u\n", offset_from_cell(node, cell));
    }
}

void debug_tombstone(struct node *node)
{
    struct cell *cell = node_cell_from_offset(node, node->tombstone_offset);
    struct cell *next_cell;

    while ((__u8 *)cell != (__u8 *)node)
    {
        LOG("tombstone: %p\n", cell);
        LOG("tombstone_size: %u\n", cell->tombstone_size);
        next_cell = node_cell_from_offset(node, cell->next_off);
        LOG("next_off: %p\n", next_cell);
        cell = next_cell;
        LOG("\n");
    }
}

void debug_node(struct node *node, int show_cells, int show_tombstones)
{
    LOG("node type: %s%s\n", node_is_leaf(node) ? "leaf" : "internal", node->flags & BTREE_NODE_FLAGS_ROOT ? " root" : "");
    LOG("node: %p\n", node);
    LOG("size: %u\n", node->size);
    LOG("tombstone_offset: %u\n", node->tombstone_offset);
    LOG("flags: %u\n", node->flags);
    LOG("last_overflow_pid: %p\n", (struct node *)node->last_overflow_pid);
    LOG("next_overflow_pid: %p\n", (struct node *)node->next_overflow_pid);
    LOG("parent_pid: %p\n", node_parent(node));
    LOG("rightmost_pid: %p\n", (struct node *)node->rightmost_pid);
    LOG("tombstone_bytes: %u\n", node->tombstone_bytes);
    LOG("cell_offset: %u\n", node->cell_offset);
    LOG("free_bytes: %lu\n", node->cell_offset - (sizeof(struct node) + sizeof(struct cell_ptr) * node->size));

    if (show_cells)
    {

        LOG("cells:\n\n");
        struct cell_ptr *cell_ptrs = node_cells(node);
        for (__u32 i = 0; i < node->size; i++)
        {
            struct cell *cell = node_cell_from_ptr(node, &cell_ptrs[i]);
            LOG("idx: %u\n", i);
            debug_node_cell(node, cell);
            LOG("\n");
        }
    }
    if (show_tombstones)
    {
        LOG("tombstones:\n\n");
        debug_tombstone(node);
    }
}

void node_cell_pointers(struct node *node, struct cell_ptr *cell_ptr, struct cell_pointers *pointers)
{
    void *cell_buf = node_cell_from_ptr(node, cell_ptr);
    struct cell *cell = cell_buf;
    if (node->flags & BTREE_NODE_FLAGS_LEAF)
    {
        pointers->key = cell_get_key(cell);
        pointers->key_size = cell->key_size;
        pointers->value = leaf_cell_get_value(cell);
        pointers->value_size = cell->value_size;
    }
    else
    {
        pointers->key = cell_get_key(cell);
        pointers->key_size = cell->key_size;
        pointers->value = NULL;
        pointers->value_size = 0;
    }
}

struct node *node_parent(struct node *node)
{
    return (struct node *)node->parent_pid;
}

void node_set_parent(struct node *node, struct node *parent)
{
    node->parent_pid = (__u64)parent;
}

void node_set_rightmost_child(struct node *node, struct node *child)
{
    node->rightmost_pid = (__u64)child;
}

void node_set_root(struct node *node)
{
    node->flags |= BTREE_NODE_FLAGS_ROOT;
}

void node_unset_root(struct node *node)
{
    node->flags &= ~BTREE_NODE_FLAGS_ROOT;
}

struct cell_ptr *node_cells(struct node *node)
{
    return (struct cell_ptr *)((__u8 *)node + sizeof(struct node));
}

struct cell_ptr *node_get_cell_ptr(struct node *node, __u32 idx)
{
    struct cell_ptr *cells = node_cells(node);

    return &cells[idx];
}

struct cell *node_cell_from_offset(struct node *node, __u32 offset)
{
    return (struct cell *)((void *)node + offset);
}

struct cell *node_cell_from_ptr(struct node *node, struct cell_ptr *cell_ptr)
{
    return node_cell_from_offset(node, cell_ptr->offset);
}

struct cell *node_cell_from_idx(struct node *node, __u32 idx)
{
    struct cell_ptr *cell_p = node_get_cell_ptr(node, idx);

    return node_cell_from_ptr(node, cell_p);
}

int key_compare(struct node *node, struct cell_ptr *cell_p, __u8 *key, __u32 key_size)
{
    struct cell *cell = node_cell_from_ptr(node, cell_p);

    return key_compare_cell(cell, key, key_size);
}

int key_compare_cell(struct cell *cell, __u8 *key, __u32 key_size)
{
    return __key_compare(cell_get_key(cell), cell->key_size, key, key_size);
}

int __key_compare(__u8 *cell_key, __u32 cell_key_size, __u8 *key, __u32 key_size)
{
    int cmp = memcmp(cell_key, key, min(key_size, cell_key_size));
    if (cmp == 0)
        cmp = key_size - cell_key_size;

    return cmp;
}

int node_bin_search(struct node *node, __u8 *key, __u32 key_size, __u32 *idx)
{
    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *cell_p;

    int cmp;
    __u32 low = 0, mid = 0, high = node->size;

    while (low < high)
    {
        mid = (low + high) / 2;
        // LOG("low: %u | mid: %u | high: %u\n", low, mid, high);

        cell_p = &cell_ptrs[mid];
        cmp = key_compare(node, cell_p, key, key_size);
        if (cmp == 0)
        {
            *idx = mid;
            return 1;
        }
        else if (cmp > 0)
            high = mid;
        else if (cmp < 0)
            low = mid + 1;
    }

    *idx = low;
    return 0;
}

struct cell_ptr *node_get_cell(struct node *node, __u8 *key, __u32 key_size)
{
    __u32 idx;
    int ret = node_bin_search(node, key, key_size, &idx);
    if (!ret)
        return NULL;

    ASSERT(idx < node->size);

    return node_get_cell_ptr(node, idx);
}

__u32 offset_from_cell(struct node *node, struct cell *cell)
{
    return (__u64)cell - (__u64)node;
}

int node_is_full(struct node *node, __u32 key_size, __u32 value_size)
{
    __u32 hdr_offset_limit = sizeof(struct node) + sizeof(struct cell_ptr) * (node->size + 1);
    __u32 free_space = node->cell_offset - hdr_offset_limit;
    __u32 new_cell_size = ALIGN(key_size + value_size + sizeof(struct cell), sizeof(__u32));

    if (free_space < new_cell_size)
    {
        struct cell *tombstone;
        __u32 next_off = node->tombstone_offset;
        // follow tombstone list
        while (next_off != 0)
        {
            tombstone = node_cell_from_offset(node, next_off);
            if (tombstone->tombstone_size >= new_cell_size)
                return 0;
            next_off = tombstone->next_off;
        }
        return 1;
    }

    return 0;
}

__u32 node_get_free_offset(struct node *node, __u32 key_size, __u32 value_size)
{
    __u32 hdr_offset_limit = sizeof(struct node) + sizeof(struct cell_ptr) * (node->size + 1);
    __u32 free_space = node->cell_offset - hdr_offset_limit;
    __u32 new_cell_size = ALIGN(key_size + value_size + sizeof(struct cell), sizeof(__u32));
    __u32 offset;

    if (free_space < new_cell_size)
    {
        struct cell *tombstone, *new_tombstone;
        // follow tombstone list
        if (node->tombstone_offset != 0)
        {
            tombstone = node_cell_from_offset(node, node->tombstone_offset);
            if (new_cell_size <= tombstone->tombstone_size)
            {
                offset = node->tombstone_offset;
                __u32 diff = tombstone->tombstone_size - new_cell_size;
                __u32 new_tombstone_offset;

                // TODO: handle remaining space in tombstone
                if (diff > ALIGN(sizeof(struct cell), sizeof(__u32)))
                {
                    new_tombstone = (struct cell *)((void *)tombstone + new_cell_size);
                    new_tombstone->tombstone_size = diff - sizeof(struct cell);
                    new_tombstone->next_off = tombstone->next_off;

                    new_tombstone_offset = offset_from_cell(node, new_tombstone);
                }
                else
                {
                    new_tombstone_offset = tombstone->next_off;
                }
                node->tombstone_offset = new_tombstone_offset;
                node->tombstone_bytes -= new_cell_size;

                // LOG("using tombstone\n");
                return offset;
            }
        }

        // TODO: check if clean space in this node (rewrite tuples in order) might save a lot of space instead of split

        // LOG("zeroed offset, free space %d\n", free_space);
        offset = 0;
    }
    else
        offset = node->cell_offset - new_cell_size;

    return offset;
}

int node_is_leaf(struct node *node)
{
    return (node->flags & BTREE_NODE_FLAGS_LEAF) != 0;
}

int node_is_root(struct node *node)
{
    return (node->flags & BTREE_NODE_FLAGS_ROOT) != 0;
}


int node_insert_nonfull(struct node *leaf, __u32 idx, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size)
{
    __u32 offset = node_get_free_offset(leaf, key_size, value_size);
    // LOG("offset: %d idx: %d len: %d\n", offset, idx, leaf->size);
    node_insert_leaf_cell(leaf, offset, idx, key, key_size, value, value_size);

    return 0;
}

__u32 node_partition_idx(struct node *node)
{

    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell *cell;

    __u32 i = 0;
    __u32 middle_bytes = 0;
    for (; i < node->size && middle_bytes < (NODE_SIZE / 2 - sizeof(struct node)); i++)
    {
        cell = node_cell_from_ptr(node, &cell_ptrs[i]);
        middle_bytes += sizeof(*cell) + cell->total_size;
    }
    return i;
}

struct node *internal_node_split(struct node *node, __u32 partition_idx)
{
    struct node *new_node = btree_node_alloc();
    ASSERT(new_node);

    node_init(new_node, 0);

    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *new_cell_ptrs = node_cells(new_node);
    struct cell *cell;

    // write first half to new node, stop when node is half empty
    // TODO: write last half to the new node, and keep the first half in the current node, if needed clean the node

    for (__u32 j = 0, k = partition_idx; k < node->size; k++, j++)
    {
        cell = node_cell_from_ptr(node, &cell_ptrs[k]);
        new_node->cell_offset -= sizeof(*cell) + cell->key_size;
        new_cell_ptrs[j].offset = new_node->cell_offset;

        node_write_internal_cell(new_node, &new_cell_ptrs[j], cell_get_key(cell), cell->key_size, internal_cell_child(cell), 0);
        node_tuple_set_tombstone(node, k);
    }

    new_node->size = node->size - partition_idx;
    node->size -= new_node->size;

    return new_node;
}

struct node *leaf_node_split(struct node *node, __u32 partition_idx)
{
    struct node *new_node = btree_node_alloc();
    ASSERT(new_node);

    node_init(new_node, BTREE_NODE_FLAGS_LEAF);

    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *new_cell_ptrs = node_cells(new_node);
    struct cell *cell;

    // write first half to new node, stop when node is half empty
    // TODO: write last half to the new node, and keep the first half in the current node, if needed clean the node

    for (__u32 j = 0, k = partition_idx; k < node->size; k++, j++)
    {
        cell = node_cell_from_ptr(node, &cell_ptrs[k]);
        new_node->cell_offset -= sizeof(*cell) + cell->key_size + cell->value_size;
        new_cell_ptrs[j].offset = new_node->cell_offset;

        node_write_leaf_cell(new_node, &new_cell_ptrs[j], cell_get_key(cell), cell->key_size, leaf_cell_get_value(cell), cell->value_size, 0);
        node_tuple_set_tombstone(node, k);
    }

    new_node->size = node->size - partition_idx;
    node->size -= new_node->size;

    return new_node;
}

int node_delete_key(struct node *node, __u8 *key, __u32 key_size)
{
    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *cell_ptr;
    __u32 idx;
    int ret;

    ret = node_bin_search(node, key, key_size, &idx);
    if (!ret)
        return 1;

    cell_ptr = node_get_cell(node, key, key_size);
    if (!cell_ptr)
        return 1;

    node_tuple_set_tombstone(node, idx);

    memmove(&cell_ptrs[idx], &cell_ptrs[idx + 1], (node->size - (idx + 1)) * sizeof(struct cell_ptr));
    node->size--;

    return 0;
}

void node_tuple_set_tombstone(struct node *node, __u32 idx)
{
    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *cell_ptr = &cell_ptrs[idx];

    // TODO: merge tombstone if neighbours
    struct cell *last_tombstone = node_cell_from_offset(node, node->tombstone_offset);

    struct cell *cell = node_cell_from_ptr(node, cell_ptr);

    cell->tombstone_size = sizeof(*cell) + cell->total_size;
    cell->next_off = node->tombstone_offset;
    node->tombstone_offset = offset_from_cell(node, cell);
    node->tombstone_bytes += cell->tombstone_size;

    (void)last_tombstone;
}

void node_insert_leaf_cell(struct node *node, __u32 offset, __u32 idx, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size)
{
    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *cell_ptr = &cell_ptrs[idx];

    // TODO: implement append to the end and mark the page as unsorted keys after certain index
    memmove(&cell_ptrs[idx + 1], &cell_ptrs[idx], (node->size - idx) * sizeof(struct cell_ptr));
    cell_ptr->offset = offset;

    // LOG("writing leaf cell at offset: %d idx: %d\n", offset, idx);
    node_write_leaf_cell(node, &cell_ptrs[idx], key, key_size, value, value_size, 0);

    node->cell_offset = offset;
    node->size++;
}

void node_insert_internal_cell(struct node *node, __u32 offset, __u32 idx, __u8 *key, __u32 key_size, struct node *child)
{
    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *cell_ptr = &cell_ptrs[idx];

    // TODO: implement append to the end and mark the page as unsorted keys after certain index
    memmove(&cell_ptrs[idx + 1], &cell_ptrs[idx], (node->size - idx) * sizeof(struct cell_ptr));
    cell_ptr->offset = offset;

    node_write_internal_cell(node, &cell_ptrs[idx], key, key_size, child, 0);

    node->cell_offset = offset;
    node->size++;
}

void node_write_leaf_cell(struct node *node, struct cell_ptr *cell_ptr, __u8 *key, __u32 key_size, __u8 *value, __u32 value_size, __u16 flags)
{
    struct cell *cell = node_cell_from_ptr(node, cell_ptr);
    cell->flags = flags;
    cell->key_size = key_size;
    cell->value_size = value_size;
    cell->total_size = key_size + value_size;
    memcpy(cell_get_key(cell), key, key_size);
    memcpy(leaf_cell_get_value(cell), value, value_size);
}

void node_write_internal_cell(struct node *node, struct cell_ptr *cell_ptr, __u8 *key, __u32 key_size, struct node *child, __u16 flags)
{
    (void)flags;

    struct cell *cell = node_cell_from_ptr(node, cell_ptr);
    cell->key_size = key_size;
    cell->total_size = key_size;
    cell->pid = (__u64)child;
    memcpy(cell_get_key(cell), key, key_size);
}