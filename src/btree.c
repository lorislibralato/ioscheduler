#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "btree.h"

void btree_cell_pointers_get(struct btree_page_hdr *hdr, struct btree_cell_ptr *cell_ptr, struct btree_cell_pointers *pointers)
{
    void *cell_buf = btree_cell_get(hdr, cell_ptr);
    if ((hdr->flags & BTREE_PAGE_FLAGS_LEAF) != 0)
    {
        struct btree_leaf_cell *cell = cell_buf;
        pointers->key = cell->content;
        pointers->key_size = cell->key_size;
        pointers->value = cell->content + cell->key_size;
        pointers->value_size = cell->value_size;
    }
    else
    {
        struct btree_internal_cell *cell = cell_buf;
        pointers->key = cell->key;
        pointers->key_size = cell->key_size;
        pointers->value = NULL;
        pointers->value_size = 0;
    }
}

struct btree_cell_ptr *btree_search(struct btree *btree, void *key, __u32 key_len)
{
    struct btree_cell_ptr *tuple_hdr = btree_node_get(btree->root, key, key_len);
    if (!tuple_hdr)
        return NULL;

    return NULL;
}

int btree_insert(struct btree *btree, void *key, __u32 key_len, void *data, __u32 data_len)
{
    return 0;
}

struct btree_page_hdr *btree_node_alloc(void)
{
    void *node = malloc(NODE_SIZE);
    ASSERT(node);

    struct btree_page_hdr *hdr = (struct btree_page_hdr *)node;
    hdr->size = 0;
    hdr->tombstone_offset = 0;
    hdr->flags = 0;
    hdr->last_overflow_pid = -1;
    hdr->next_overflow_pid = -1;
    hdr->rightmost_pid = -1;
    hdr->tombstone_bytes = 0;
    hdr->cell_offset = NODE_SIZE;

    return hdr;
}

struct btree_cell_ptr *btree_cells(struct btree_page_hdr *hdr)
{
    return (struct btree_cell_ptr *)((__u8 *)hdr + sizeof(struct btree_page_hdr));
}

struct btree_cell_ptr *btree_get_cell_ptr(struct btree_page_hdr *hdr, __u32 idx)
{
    struct btree_cell_ptr *cells = btree_cells(hdr);

    return &cells[idx];
}

void *btree_cell_get(struct btree_page_hdr *hdr, struct btree_cell_ptr *cell_ptr)
{
    return (void *)hdr + cell_ptr->offset;
}

int btree_tuple_compare(struct btree_page_hdr *hdr, struct btree_cell_ptr *cell_ptr, void *key, __u32 key_len)
{
    int cmp;
    void *cell_buf;
    void *cell_key_buf;
    __u32 cell_key_size;

    // TOOD: handle key_size < 4
    cmp = *(__u32 *)key - cell_ptr->key_prefix;
    if (cmp == 0)
    {
        cell_buf = btree_cell_get(hdr, cell_ptr);
        if ((hdr->flags & BTREE_PAGE_FLAGS_LEAF) != 0)
        {
            struct btree_leaf_cell *cell = (struct btree_leaf_cell *)cell_buf;
            cell_key_size = cell->key_size;
            cell_key_buf = cell->content;
        }
        else
        {
            struct btree_internal_cell *cell = (struct btree_internal_cell *)cell_buf;
            cell_key_size = cell->key_size;
            cell_key_buf = cell->key;
        }

        cmp = memcmp(key, cell_key_buf, min(key_len, cell_key_size));
        if (cmp == 0)
            cmp = key_len - cell_key_size;
    }
    return cmp;
}

int btree_node_bin_search(struct btree_page_hdr *hdr, void *key, __u32 key_len, __u32 *idx)
{
    struct btree_cell_ptr *cells = btree_cells(hdr);
    struct btree_cell_ptr *cell;

    int cmp;
    __u32 low = 0, mid = 0, high = hdr->size;

    while (low < high)
    {
        mid = (low + high) / 2;
        // LOG("low: %u | mid: %u | high: %u\n", low, mid, high);

        cell = &cells[mid];
        cmp = btree_tuple_compare(hdr, cell, key, key_len);
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

struct btree_cell_ptr *btree_node_get(struct btree_page_hdr *hdr, void *key, __u32 key_len)
{
    __u32 idx;
    int ret = btree_node_bin_search(hdr, key, key_len, &idx);
    if (!ret)
        return NULL;

    ASSERT(idx < hdr->size);

    return btree_get_cell_ptr(hdr, idx);
}

int btree_leaf_node_insert(struct btree_page_hdr *hdr, void *key, __u32 key_len, void *data, __u32 data_len)
{
    struct btree_cell_ptr *cell_ptrs = btree_cells(hdr);
    struct btree_cell_ptr *cell_ptr;

    __u32 hdr_offset_limit = sizeof(struct btree_page_hdr) + sizeof(struct btree_cell_ptr) * (hdr->size + 1);
    __u32 offset;
    __u32 free_space = hdr->cell_offset - hdr_offset_limit;
    __u32 new_cell_size = ALIGN(key_len + data_len + sizeof(struct btree_leaf_cell), sizeof(__u32));

    if (free_space < new_cell_size)
    {
        // follow tombstone list
        if (hdr->tombstone_offset != 0)
        {
            struct btree_cell_tombstone *tombstone = (struct btree_cell_tombstone *)((__u8 *)hdr + hdr->tombstone_offset);
            if (new_cell_size <= tombstone->size)
            {
                offset = hdr->tombstone_offset;
                __u32 diff = tombstone->size - new_cell_size;
                __u32 new_tombstone_offset;

                // TODO: handle remaining space in tombstone
                if (diff > sizeof(struct btree_cell_tombstone))
                {
                    struct btree_cell_tombstone *new_tombstone = (struct btree_cell_tombstone *)((void *)tombstone + new_cell_size);
                    new_tombstone->size = diff - sizeof(struct btree_cell_tombstone);
                    new_tombstone->next_tombstone_offset = tombstone->next_tombstone_offset;

                    new_tombstone_offset = (__u64)hdr - (__u64)new_tombstone;
                }
                else
                {
                    new_tombstone_offset = tombstone->next_tombstone_offset;
                }
                hdr->tombstone_offset = new_tombstone_offset;
            }
        }

        // TODO: clean space in this node and retry
        {
        }

        // no space left
        return -1;
    }
    else
        offset = hdr->cell_offset - new_cell_size;

    __u32 idx;
    int ret = btree_node_bin_search(hdr, key, key_len, &idx);
    ASSERT(!ret); // TODO: handle key already exists

    // TODO: implement append to the end and mark the page as unsorted keys after certain index
    memmove(&cell_ptrs[idx + 1], &cell_ptrs[idx], (hdr->size - idx) * sizeof(struct btree_cell_ptr));
    cell_ptr = &cell_ptrs[idx];
    cell_ptr->offset = offset;
    cell_ptr->key_prefix = *(__u32 *)key;

    struct btree_leaf_cell *cell = (struct btree_leaf_cell *)((void *)hdr + offset);
    cell->flags = 0;
    cell->key_size = key_len;
    cell->value_size = data_len;
    memcpy((void *)cell->content, key, key_len);
    memcpy((void *)cell->content + key_len, data, data_len);

    hdr->cell_offset = offset;
    hdr->size++;
    // LOG("insert \"%s\" in idx: %u\n", (__u8 *)key, idx);

    return 0;
}
