#include "tree/cell.h"
#include "utils.h"

void debug_cell(struct cell *cell)
{
    LOG("cell %p\n", cell);
    LOG("key_size: %u\n", cell->key_size);
    LOG("total_size: %u\n", cell->total_size);
    LOG("key: %.*s\n", cell->key_size, cell_get_key(cell));
}

__u8 *leaf_cell_get_value(struct cell *cell)
{
    return cell->content + cell->key_size;
}

__u8 *cell_get_key(struct cell *cell)
{
    return &cell->content[0];
}

struct node *internal_cell_child(struct cell *cell)
{
    return (struct node *)cell->pid;
}