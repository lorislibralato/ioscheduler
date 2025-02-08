#include <stdlib.h>
#include <string.h>
#include "utils.h"

#include "tree/node.h"
#include "tree/cell.h"
#include "tree/btree.h"

void node_init(struct node *node)
{
    node->size = 0;
    node->tombstone_offset = 0;
    node->flags = 0;
    node->last_overflow_pid = -1;
    node->next_overflow_pid = -1;
    node->rightmost_pid = -1;
    node->tombstone_bytes = 0;
    node->cell_offset = NODE_SIZE;
}

void cell_pointers_get(struct node *node, struct cell_ptr *cell_ptr, struct btree_cell_pointers *pointers)
{
    void *cell_buf = node_cell_from_ptr(node, cell_ptr);
    struct cell *cell = cell_buf;
    if (node->flags & BTREE_PAGE_FLAGS_LEAF)
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

int key_compare(struct node *node, struct cell_ptr *cell_p, void *key, __u32 key_size)
{
    struct cell *cell = node_cell_from_ptr(node, cell_p);

    int cmp = memcmp(key, cell_get_key(cell), min(key_size, cell->key_size));
    if (cmp == 0)
        cmp = key_size - cell->key_size;

    return cmp;
}

int node_bin_search(struct node *node, void *key, __u32 key_size, __u32 *idx)
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
            low = mid + 1;
        else if (cmp < 0)
            high = mid;
    }

    *idx = low;
    return 0;
}

struct cell_ptr *btree_node_get(struct node *hdr, void *key, __u32 key_size)
{
    __u32 idx;
    int ret = node_bin_search(hdr, key, key_size, &idx);
    if (!ret)
        return NULL;

    ASSERT(idx < hdr->size);

    return node_get_cell_ptr(hdr, idx);
}

__u32 offset_from_cell(struct node *hdr, void *cell)
{
    return (__u64)cell - (__u64)hdr;
}

__u32 node_get_free_offset(struct node *node, __u32 key_size, __u32 value_size)
{
    __u32 hdr_offset_limit = sizeof(struct node) + sizeof(struct cell_ptr) * (node->size + 1);
    __u32 offset;
    __u32 free_space = node->cell_offset - hdr_offset_limit;
    __u32 new_cell_size = ALIGN(key_size + value_size + sizeof(struct cell), sizeof(__u32));

    if (free_space < new_cell_size)
    {
        // follow tombstone list
        if (node->tombstone_offset != 0)
        {
            struct cell *tombstone = node_cell_from_offset(node, node->tombstone_offset);
            if (new_cell_size <= tombstone->tombstone_size)
            {
                offset = node->tombstone_offset;
                __u32 diff = tombstone->tombstone_size - new_cell_size;
                __u32 new_tombstone_offset;

                // TODO: handle remaining space in tombstone
                if (diff > sizeof(struct cell))
                {
                    struct cell *new_tombstone = (struct cell *)((void *)tombstone + new_cell_size);
                    new_tombstone->tombstone_size = diff - sizeof(struct cell);
                    new_tombstone->next_off = tombstone->next_off;

                    new_tombstone_offset = offset_from_cell(node, new_tombstone);
                }
                else
                {
                    new_tombstone_offset = tombstone->next_off;
                }
                node->tombstone_offset = new_tombstone_offset;

                return offset;
            }
        }

        // TODO: check if clean space in this node (rewrite tuples in order) might save a lot of space instead of split

        offset = 0;
    }
    else
        offset = node->cell_offset - new_cell_size;

    return offset;
}

int node_is_leaf(struct node *node)
{
    return node->flags & BTREE_PAGE_FLAGS_LEAF;
}

int node_insert(struct node *leaf, void *key, __u32 key_size, void *value, __u32 value_size)
{
    struct cell_ptr *cell_ptrs;
    struct cell_ptr *cell_ptr;
    struct cell *internal_cell;
    __u32 idx;
    int ret;

    while (1)
    {
        cell_ptrs = node_cells(leaf);
        ret = node_bin_search(leaf, key, key_size, &idx);
        if (node_is_leaf(leaf))
            break;

        cell_ptr = &cell_ptrs[idx];
        internal_cell = node_cell_from_ptr(leaf, cell_ptr);
        // TODO: resolve pid to page in memory
        leaf = internal_cell_node(internal_cell);
    }

    // key already exists in leaf node
    if (ret)
        return -1;

    __u32 offset = node_get_free_offset(leaf, key_size, value_size);
    if (!offset)
    {
        // split leaf node
        __u32 partition_idx = node_partition_idx(leaf);
        struct node *new_node = leaf_node_split(leaf, partition_idx);
        struct cell_ptr *new_node_first_cell_ptr = &node_cells(new_node)[0];
        struct cell *new_node_first_cell = node_cell_from_ptr(new_node, new_node_first_cell_ptr);
        struct node *target_node;

        // if key is more than first cell of new node insert in the new node
        if (key_compare(new_node, new_node_first_cell_ptr, key, key_size) >= 0)
            target_node = new_node;
        else
            target_node = leaf;

        offset = node_get_free_offset(target_node, key_size, value_size);
        ASSERT(offset > 0);
        ret = node_bin_search(target_node, key, key_size, &idx);
        ASSERT(ret == 0);

        // TODO: if insert is in the new node, just insert it during cell copying
        btree_insert_leaf_cell(target_node, offset, idx, key, key_size, value, value_size);

        // insert partion key in the parent internal node and link the new node
        struct node *internal_node = (struct node *)leaf->parent_pid;
        struct node *new_internal_node;
        struct node *child_node = new_node;
        void *promoted_key = cell_get_key(new_node_first_cell);
        __u32 promoted_key_size = new_node_first_cell->key_size;
        int split_node;

        while (1)
        {
            // parent node doesn't have space, split internal node recusivly
            offset = node_get_free_offset(internal_node, promoted_key_size, 0);
            split_node = !offset;
            if (split_node)
            {
                partition_idx = node_partition_idx(internal_node);
                new_internal_node = internal_node_split(internal_node, partition_idx + 1);

                struct cell_ptr *internal_node_cell_ptrs = node_cells(internal_node);

                if (key_compare(internal_node, &internal_node_cell_ptrs[partition_idx], promoted_key, promoted_key_size) >= 0)
                    target_node = new_internal_node;
                else
                    target_node = internal_node;
            }
            else
            {
                target_node = internal_node;
            }

            ret = node_bin_search(target_node, promoted_key, promoted_key_size, &idx);
            ASSERT(ret == 0);
            offset = node_get_free_offset(target_node, promoted_key_size, 0);
            ASSERT(offset > 0);

            btree_insert_internal_cell(target_node, child_node, offset, idx, promoted_key, promoted_key_size);

            if (!split_node)
                break;

            // new partition key that wasn't included in the new internal node but need to be promoted to the upper node
            struct cell_ptr *internal_partition_cell_ptr = &node_cells(internal_node)[partition_idx];
            struct cell *internal_partition_cell = node_cell_from_ptr(internal_node, internal_partition_cell_ptr);
            promoted_key = cell_get_key(internal_partition_cell);
            promoted_key_size = internal_partition_cell->key_size;

            child_node = new_internal_node;
            internal_node = (struct node *)internal_node->parent_pid;
        }
        // TODO: promoted key should point to old node
    }
    else
    {
        btree_insert_leaf_cell(leaf, offset, idx, key, key_size, value, value_size);
    }
    // LOG("insert \"%s\" in idx: %u\n", (__u8 *)key, idx);

    return 0;
}

__u32 node_partition_idx(struct node *node)
{

    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell *cell;

    __u32 i = 0;
    __u32 middle_bytes = 0;
    for (; i < node->size && middle_bytes < NODE_SIZE / 2; i++)
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

    // TODO: init node leaf or internal
    node_init(new_node);

    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *new_cell_ptrs = node_cells(new_node);
    struct cell *cell;

    // write first half to new node, stop when node is half empty
    // TODO: write last half to the new node, and keep the first half in the current node, if needed clean the node

    for (__u32 j = 0; partition_idx < node->size; partition_idx++, j++)
    {

        cell = node_cell_from_ptr(node, &cell_ptrs[partition_idx]);
        new_node->cell_offset -= sizeof(*cell) + cell->key_size;
        new_cell_ptrs[j].offset = new_node->cell_offset;

        btree_write_internal_cell(new_node, internal_cell_node(cell), &new_cell_ptrs[j], cell_get_key(cell), cell->key_size, 0);
        btree_tuple_set_tombstone(node, partition_idx);
    }

    return new_node;
}

struct node *leaf_node_split(struct node *node, __u32 partition_idx)
{
    struct node *new_node = btree_node_alloc();
    ASSERT(new_node);

    // TODO: init node leaf or internal
    node_init(new_node);
    new_node->parent_pid = (__u64)node;

    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *new_cell_ptrs = node_cells(new_node);
    struct cell *cell;

    // write first half to new node, stop when node is half empty
    // TODO: write last half to the new node, and keep the first half in the current node, if needed clean the node

    for (__u32 j = 0; partition_idx < node->size; partition_idx++, j++)
    {
        cell = node_cell_from_ptr(node, &cell_ptrs[partition_idx]);
        new_node->cell_offset -= sizeof(*cell) + cell->key_size + cell->value_size;
        new_cell_ptrs[j].offset = new_node->cell_offset;

        btree_write_leaf_cell(new_node, &new_cell_ptrs[j], cell_get_key(cell), cell->key_size, leaf_cell_get_value(cell), cell->value_size, 0);
        btree_tuple_set_tombstone(node, partition_idx);
    }

    return new_node;
}

void btree_tuple_set_tombstone(struct node *node, __u32 idx)
{
    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *cell_ptr = &cell_ptrs[idx];

    // TODO: merge tombstone if neighbours
    struct cell *last_tombstone = node_cell_from_offset(node, node->tombstone_offset);

    struct cell *cell = node_cell_from_ptr(node, cell_ptr);

    cell->tombstone_size = sizeof(*cell) + cell->total_size;
    cell->next_off = node->tombstone_offset;
    node->tombstone_offset = offset_from_cell(node, cell);

    (void)last_tombstone;
}

void btree_insert_leaf_cell(struct node *hdr, __u32 offset, __u32 idx, void *key, __u32 key_size, void *value, __u32 value_size)
{
    struct cell_ptr *cell_ptrs = node_cells(hdr);
    struct cell_ptr *cell_ptr = &cell_ptrs[idx];

    // TODO: implement append to the end and mark the page as unsorted keys after certain index
    memmove(&cell_ptrs[idx + 1], &cell_ptrs[idx], (hdr->size - idx) * sizeof(struct cell_ptr));
    cell_ptr->offset = offset;

    btree_write_leaf_cell(hdr, &cell_ptrs[idx], key, key_size, value, value_size, 0);

    hdr->cell_offset = offset;
    hdr->size++;
}

void btree_insert_internal_cell(struct node *node, struct node *child, __u32 offset, __u32 idx, void *key, __u32 key_size)
{
    struct cell_ptr *cell_ptrs = node_cells(node);
    struct cell_ptr *cell_ptr = &cell_ptrs[idx];

    // TODO: implement append to the end and mark the page as unsorted keys after certain index
    memmove(&cell_ptrs[idx + 1], &cell_ptrs[idx], (node->size - idx) * sizeof(struct cell_ptr));
    cell_ptr->offset = offset;

    btree_write_internal_cell(node, child, &cell_ptrs[idx], key, key_size, 0);

    node->cell_offset = offset;
    node->size++;
}

void btree_write_leaf_cell(struct node *node, struct cell_ptr *cell_ptr, void *key, __u32 key_size, void *value, __u32 value_size, __u16 flags)
{
    struct cell *cell = node_cell_from_ptr(node, cell_ptr);
    cell->flags = flags;
    cell->key_size = key_size;
    cell->value_size = value_size;
    cell->total_size = key_size + value_size;
    memcpy(cell_get_key(cell), key, key_size);
    memcpy(leaf_cell_get_value(cell), value, value_size);
}

void btree_write_internal_cell(struct node *node, struct node *child, struct cell_ptr *cell_ptr, void *key, __u32 key_size, __u16 flags)
{
    (void)flags;

    struct cell *cell = node_cell_from_ptr(node, cell_ptr);
    cell->key_size = key_size;
    cell->total_size = key_size;
    cell->pid = (__u64)child;
    memcpy(cell_get_key(cell), key, key_size);
}