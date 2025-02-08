#define ASSERTION
#define DEBUG
#include <string.h>
#include "../src/include/utils.h"
#include "../src/include/tree/btree.h"
#include "../src/include/tree/node.h"
#include "../src/include/tree/cell.h"

void insert_and_test(struct node *hdr, void *key, void *data)
{
    int ret;
    __u32 size = hdr->size;
    __u32 offset = hdr->cell_offset;
    ret = node_insert(hdr, key, strlen(key), data, strlen(data));
    ASSERT(!ret);

    ASSERT(hdr->size == size + 1);
    ASSERT(hdr->cell_offset == offset - ALIGN(sizeof(struct cell) + strlen(key) + strlen(data), sizeof(__u32)));

    __u32 idx;
    ret = node_bin_search(hdr, key, strlen(key), &idx);
    ASSERT(ret);
    // LOG("inserting in index: %d\n", idx);

    struct cell_ptr *cell_ptr;

    cell_ptr = node_get_cell(hdr, key, strlen(key));
    ASSERT(cell_ptr);
    // ASSERT(cell_ptr->key_prefix == *(__u32 *)key);

    struct cell_pointers pointers;
    node_cell_pointers(hdr, cell_ptr, &pointers);

    ASSERT(pointers.key_size == strlen(key));
    ASSERT(memcmp(pointers.key, key, strlen(key)) == 0);
    ASSERT(pointers.value_size == strlen(data));
    ASSERT(memcmp(pointers.value, data, strlen(data)) == 0);

    // LOG("key: %s OK\n", (char *)key);
}

void check_index(struct node *hdr, void *key, __u32 idx)
{
    struct cell_ptr *tuple_hdr;

    tuple_hdr = node_get_cell(hdr, key, strlen(key));
    ASSERT(tuple_hdr);
    ASSERT(&(node_cells(hdr)[idx]) == tuple_hdr);
}

int main()
{
    struct node *hdr = btree_node_alloc();
    ASSERT(hdr);
    node_init(hdr);
    hdr->flags |= BTREE_PAGE_FLAGS_LEAF;

    insert_and_test(hdr, "test2", "data");
    insert_and_test(hdr, "test3", "data");
    insert_and_test(hdr, "test1", "data");
    insert_and_test(hdr, "test0", "data");

    check_index(hdr, "test0", 0);
    check_index(hdr, "test1", 1);
    check_index(hdr, "test2", 2);
    check_index(hdr, "test3", 3);

    LOG("TEST (%s): ok\n", __FILE__);
}