#include "swap.h"

static size_t num_swap_slots;

/*!
 *  Implementation of swap table
 *  The swap table contains whether a swap slot (corresponding to a page)
 *  is occupied
 */
void swap_table_init(void) {
    struct block *swap = block_get_role(BLOCK_SWAP);
    block_sector_t num_swap_sectors = block_size(swap);
    /* Allocate one swap slot per page */
    num_swap_slots
        = (size_t) (BLOCK_SECTOR_SIZE * num_swap_sectors / PGSIZE);
    swap_table = bitmap_create(num_swap_slots);
    if (swap_table == NULL) {
        PANIC("Failed to create swap table");
    }
}

/*!
 *  Return an index (slot) in the swap table that can fit a page and
 *  mark it as occupied in the swap table
 */
size_t swap_get_slot(void) {
    size_t slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
    if (slot == BITMAP_ERROR) {
        PANIC("Swap table is full");
    }
    return slot;
}

/*
 * Add other functions
 */
