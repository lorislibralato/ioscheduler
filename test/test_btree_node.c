#include <stdio.h>
#include <string.h>
#include "../src/btree.h"
#include "../src/utils.h"

void insert_and_test(struct btree_node_hdr *hdr, void *key, void *data)
{
    int ret;
    __u16 len = hdr->len;
    __u16 offset = hdr->tuple_offset_limit;
    ret = btree_node_insert(hdr, key, strlen(key), data, strlen(data));
    assert(!ret);

    assert(hdr->len == len + 1);
    assert(hdr->tuple_offset_limit == offset - strlen(key) - strlen(data));

    struct btree_tuple_hdr *tuple_hdr;

    tuple_hdr = btree_node_get(hdr, key, strlen(key));
    assert(tuple_hdr);
    assert(tuple_hdr->key_prefix == *(__u8 *)key);

    struct btree_tuple_pointers tuple_p;
    btree_tuple_get_pointers(hdr, tuple_hdr, &tuple_p);
    assert(tuple_p.key_len == strlen(key));
    assert(memcmp(tuple_p.key, key, strlen(key)) == 0);
    assert(tuple_p.data_len == strlen(data));
    assert(memcmp(tuple_p.data, data, strlen(data)) == 0);
}

void check_index(struct btree_node_hdr *hdr, void *key, __u16 idx)
{
    struct btree_tuple_hdr *tuple_hdr;

    tuple_hdr = btree_node_get(hdr, key, strlen(key));
    assert(tuple_hdr);
    assert(&(btree_tuple_get_hdrs(hdr)[idx]) == tuple_hdr);
}

int main()
{
    struct btree_node_hdr *hdr = btree_node_alloc();
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