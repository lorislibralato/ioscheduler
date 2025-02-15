#define ASSERTION
#define DEBUG
#include <string.h>
#include "../src/include/utils.h"
#include "../src/include/tree/btree.h"
#include "../src/include/tree/node.h"
#include "../src/include/tree/cell.h"

void insert_and_test(struct node *node, void *key, void *data)
{
    int ret;
    __u32 size = node->size;
    __u32 offset = node->cell_offset;

    __u32 idx;
    ret = node_bin_search(node, key, strlen(key), &idx);
    ASSERT(!ret);

    ret = node_insert_nonfull(node, idx, key, strlen(key), data, strlen(data));
    ASSERT(!ret);

    ret = node_bin_search(node, key, strlen(key), &idx);
    ASSERT(ret);

    ASSERT(node->size == size + 1);
    ASSERT(node->cell_offset == offset - ALIGN(sizeof(struct cell) + strlen(key) + strlen(data), sizeof(__u32)));

    struct cell_ptr *cell_ptr;

    cell_ptr = node_get_cell(node, key, strlen(key));
    ASSERT(cell_ptr);

    struct cell_pointers pointers;
    node_cell_pointers(node, cell_ptr, &pointers);

    ASSERT(pointers.key_size == strlen(key));
    ASSERT(memcmp(pointers.key, key, strlen(key)) == 0);
    ASSERT(pointers.value_size == strlen(data));
    ASSERT(memcmp(pointers.value, data, strlen(data)) == 0);
}

void check_index(struct node *node, void *key, __u32 idx)
{
    struct cell_ptr *cell;

    cell = node_get_cell(node, key, strlen(key));
    ASSERT(cell);
    ASSERT(&(node_cells(node)[idx]) == cell);
}

void delete_key(struct node *node, void *key)
{
    struct cell_ptr *cell;

    int err;
    err = node_delete_key(node, key, strlen(key));
    ASSERT(!err);
    

    cell = node_get_cell(node, key, strlen(key));
    ASSERT(cell == NULL);
}

void test_insert_position()
{
    struct node *node = btree_node_alloc();
    ASSERT(node);
    node_init(node, BTREE_NODE_FLAGS_LEAF);

    insert_and_test(node, "test2", "data");
    insert_and_test(node, "test3", "data");
    insert_and_test(node, "test1", "data");
    insert_and_test(node, "test0", "data");
    insert_and_test(node, "test9", "data");
    insert_and_test(node, "test8", "data");
    insert_and_test(node, "test7", "data");

    check_index(node, "test0", 0);
    check_index(node, "test1", 1);
    check_index(node, "test2", 2);
    check_index(node, "test3", 3);
    check_index(node, "test7", 4);
    check_index(node, "test8", 5);
    check_index(node, "test9", 6);

    delete_key(node, "test0");
    delete_key(node, "test1");

    check_index(node, "test2", 0);
    check_index(node, "test3", 1);
    check_index(node, "test7", 2);
    check_index(node, "test8", 3);
    check_index(node, "test9", 4);


    LOG("TEST (%s:%s): ok\n", __FILE__, __FUNCTION__);
}

int main()
{
    test_insert_position();
}