#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "btree.h"

struct btree_node_hdr *btree_node_alloc(void)
{
    void *node = malloc(NODE_SIZE);
    ASSERT(node);

    struct btree_node_hdr *hdr = (struct btree_node_hdr *)node;
    hdr->len = 0;
    hdr->tombstone_offset = 0;
    hdr->tuple_offset_limit = NODE_SIZE;

    return hdr;
}

struct btree_tuple_hdr *btree_tuple_get_hdrs(struct btree_node_hdr *hdr)
{
    return (struct btree_tuple_hdr *)((__u8 *)hdr + sizeof(struct btree_node_hdr));
}

struct btree_tuple_hdr *btree_tuple_get_hdr(struct btree_node_hdr *hdr, __u16 idx)
{
    struct btree_tuple_hdr *tuple_hdrs = btree_tuple_get_hdrs(hdr);

    return &tuple_hdrs[idx];
}

__u8 *btree_tuple_get(struct btree_node_hdr *hdr, struct btree_tuple_hdr *tuple_hdr)
{
    return (__u8 *)hdr + tuple_hdr->offset;
}

int btree_tuple_compare(struct btree_node_hdr *hdr, struct btree_tuple_hdr *tuple_hdr, void *key, __u16 key_len)
{
    int cmp;
    __u8 *tuple;

    cmp = *(__u8 *)key - tuple_hdr->key_prefix;
    if (cmp == 0)
    {
        tuple = btree_tuple_get(hdr, tuple_hdr);
        cmp = memcmp((__u8 *)key, tuple, min(key_len, tuple_hdr->key_len));
        if (cmp == 0)
            cmp = key_len - tuple_hdr->key_len;
    }
    return cmp;
}

int btree_node_bin_search(struct btree_node_hdr *hdr, void *key, __u16 key_len, __u16 *idx)
{
    struct btree_tuple_hdr *tuple_hdrs = btree_tuple_get_hdrs(hdr);
    struct btree_tuple_hdr *tuple_hdr;

    int cmp;
    __u16 low = 0, mid = 0, high = hdr->len;

    while (low < high)
    {
        mid = (low + high) / 2;
        // LOG("low: %u | mid: %u | high: %u\n", low, mid, high);

        tuple_hdr = &tuple_hdrs[mid];
        cmp = btree_tuple_compare(hdr, tuple_hdr, key, key_len);
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

struct btree_tuple_hdr *btree_node_get(struct btree_node_hdr *hdr, void *key, __u16 key_len)
{
    __u16 idx;
    int ret = btree_node_bin_search(hdr, key, key_len, &idx);
    ASSERT(idx < hdr->len);

    if (!ret)
        return NULL;

    return btree_tuple_get_hdr(hdr, idx);
}

void btree_tuple_get_pointers(struct btree_node_hdr *hdr, struct btree_tuple_hdr *tuple_hdr, struct btree_tuple_pointers *out)
{
    __u8 *tuple = btree_tuple_get(hdr, tuple_hdr);

    out->key = tuple;
    out->data = tuple + tuple_hdr->key_len;
    out->data_len = tuple_hdr->data_len;
    out->key_len = tuple_hdr->key_len;
}

int btree_node_insert(struct btree_node_hdr *hdr, void *key, __u16 key_len, void *data, __u16 data_len)
{
    struct btree_tuple_hdr *tuple_hdrs = btree_tuple_get_hdrs(hdr);
    struct btree_tuple_hdr *tuple_hdr;

    __u16 hdr_offset_limit = sizeof(struct btree_node_hdr) + sizeof(struct btree_tuple_hdr) * (hdr->len + 1);
    __u16 offset;

    if (hdr->tuple_offset_limit - hdr_offset_limit < data_len + key_len)
    {
        // follow tombstone list
        if (hdr->tombstone_offset != 0)
        {
            struct btree_tuple_tombstone *tombstone = (struct btree_tuple_tombstone *)((__u8 *)hdr + hdr->tombstone_offset);
            struct btree_tuple_hdr *tombstone_hdr = &tuple_hdrs[tombstone->tuple_hdr_index];
            if (hdr->tuple_offset_limit - hdr_offset_limit >= tombstone_hdr->data_len + tombstone_hdr->key_len)
            {
                offset = (__u16)((__u8 *)tombstone - (__u8 *)hdr);
                hdr->tombstone_offset = tombstone->next_tombstone_offset;
            }
        }

        // TODO: clean space in this node and retry
        {
        }

        // no space left
        return -1;
    }
    else
        offset = hdr->tuple_offset_limit - key_len - data_len;

    __u16 idx;
    int ret = btree_node_bin_search(hdr, key, key_len, &idx);
    ASSERT(!ret); // TODO: handle key already exists

    memmove(&tuple_hdrs[idx + 1], &tuple_hdrs[idx], (hdr->len - idx) * sizeof(struct btree_tuple_hdr));
    tuple_hdr = &tuple_hdrs[idx];
    tuple_hdr->flags = 0;
    tuple_hdr->key_prefix = *(__u8 *)key;
    tuple_hdr->key_len = key_len;
    tuple_hdr->data_len = data_len;
    tuple_hdr->offset = offset;

    memcpy((__u8 *)hdr + offset, key, key_len);
    memcpy((__u8 *)hdr + offset + key_len, data, data_len);

    hdr->tuple_offset_limit = offset;
    hdr->len++;

    LOG("insert \"%s\" in idx: %u\n", (__u8 *)key, idx);

    return 0;
}
