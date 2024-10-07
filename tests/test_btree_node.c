#include <stdio.h>
#include <string.h>
#include "../src/btree.h"
#include "../src/utils.h"
#define ASSERTION

void insert_and_test(struct btree_page_hdr *hdr, void *key, void *data)
{
    int ret;
    __u32 size = hdr->size;
    __u32 offset = hdr->cell_offset;
    ret = btree_leaf_node_insert(hdr, key, strlen(key), data, strlen(data));
    assert(!ret);

    assert(hdr->size == size + 1);
    assert(hdr->cell_offset == offset - ALIGN(sizeof(struct btree_leaf_cell) + strlen(key) + strlen(data), sizeof(__u32)));

    __u32 idx;
    ret = btree_node_bin_search(hdr, key, strlen(key), &idx);
    ASSERT(ret);
    // LOG("inserting in index: %d\n", idx);

    struct btree_cell_ptr *cell_ptr;

    cell_ptr = btree_node_get(hdr, key, strlen(key));
    assert(cell_ptr);
    assert(cell_ptr->key_prefix == *(__u32 *)key);

    struct btree_cell_pointers pointers;
    btree_cell_pointers_get(hdr, cell_ptr, &pointers);

    assert(pointers.key_size == strlen(key));
    assert(memcmp(pointers.key, key, strlen(key)) == 0);
    assert(pointers.value_size == strlen(data));
    assert(memcmp(pointers.value, data, strlen(data)) == 0);

    // LOG("key: %s OK\n", (char *)key);
}

void check_index(struct btree_page_hdr *hdr, void *key, __u32 idx)
{
    struct btree_cell_ptr *tuple_hdr;

    tuple_hdr = btree_node_get(hdr, key, strlen(key));
    assert(tuple_hdr);
    assert(&(btree_cells(hdr)[idx]) == tuple_hdr);
}

int main()
{
    struct btree_page_hdr *hdr = btree_node_alloc();
    hdr->flags |= BTREE_PAGE_FLAGS_LEAF;
    assert(hdr);

    insert_and_test(hdr, "test2", "data");
    insert_and_test(hdr, "test3", "data");
    insert_and_test(hdr, "test1", "data");
    insert_and_test(hdr, "test0", "data");

    check_index(hdr, "test0", 0);
    check_index(hdr, "test1", 1);
    check_index(hdr, "test2", 2);
    check_index(hdr, "test3", 3);

    printf("TEST (%s): ok\n", __FILE__);
}