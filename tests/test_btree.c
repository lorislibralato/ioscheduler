#define ASSERTION
#define DEBUG
#include <string.h>
#include "../src/include/utils.h"
#include "../src/include/tree/btree.h"
#include "../src/include/tree/node.h"
#include "../src/include/tree/cell.h"

#define TUPLE_COUNT (900)

int main()
{
    struct btree btree;
    int ret;

    ret = btree_init(&btree);
    ASSERT(!ret);

    __u8 key[8];
    __u8 value[8];

    for (__u64 i = 0; i < TUPLE_COUNT; i++)
    {
        *(__u64 *)key = i;
        *(__u64 *)value = i;

        ret = btree_insert(&btree, (void *)key, ARRAY_LEN(key), (void *)value, ARRAY_LEN(value));
        ASSERT(!ret);

        for (__u32 j = 0; j < btree.root->size; j++)
        {
            struct cell_ptr *cells = node_cells(btree.root);
            struct cell *cell = node_cell_from_ptr(btree.root, &cells[j]);
        }
    }

    LOG("root is leaf: %d\n", is_node_leaf(btree.root));

    LOG("TEST (%s): ok\n", __FILE__);
}
