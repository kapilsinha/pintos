/* Swap table implementation. */

#include "bitmap.h"
#include "devices/block.h"
#include "threads/vaddr.h"

struct bitmap *swap_table;

void swap_table_init(void);
size_t swap_get_slot(void);
