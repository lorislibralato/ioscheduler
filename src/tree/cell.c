#include "tree/cell.h"


void *leaf_cell_value(struct cell *cell)
{
    return (void *)cell->content + cell->key_size;
}