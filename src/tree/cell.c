#include "tree/cell.h"

void *leaf_cell_get_value(struct cell *cell)
{
    return (void *)(cell->content + cell->key_size);
}

void *cell_get_key(struct cell *cell)
{
    return (void *)cell->content;
}

struct node *internal_cell_node(struct cell *cell)
{
    return (struct node *)cell->pid;
}