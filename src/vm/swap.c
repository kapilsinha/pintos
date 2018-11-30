#include "swap.h"
#include "frame.h"

/* Number of sectors in a page. 512 * 8 = 4096. */
#define NUM_SECTORS 8

static size_t num_swap_slots;
static struct block* swap;

/*!
 *  Implementation of swap table
 *  The swap table contains whether a swap slot (corresponding to a page)
 *  is occupied
 */
void swap_table_init(void) {
    swap = block_get_role(BLOCK_SWAP);
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

/*!
 * Writes a page to an available swap slot. Assumes that upage is actually a user
 * page and points to memory the size of a page. Returns the slot that the page
 * was written to.
 */
size_t swap_write(void *upage) {
    size_t slot = swap_get_slot() * 8;
    // Buffer for one block
    void *buffer = malloc(BLOCK_SECTOR_SIZE);
    // Write to the swap block, beginning at sector "slot"
    for (int i = 0; i < 8; i++) {
        // Copy over file contents to the buffer
        memcpy(buffer, upage, BLOCK_SECTOR_SIZE);
        // Write the buffer to swap
        block_write(swap, slot + i, buffer);
        upage += BLOCK_SECTOR_SIZE;
    }
    return slot;
}

/*!
 * Reads a page from given swap slot into upage. Assumes that slot number is a
 * multiple of 8 and that upage points to a user page.
 */
void swap_read(void *upage, size_t slot) {
    ASSERT(slot % 8 == 0);
    // Buffer for one block
    void *buffer = malloc(BLOCK_SECTOR_SIZE);
    // Write to the swap block, beginning at sector "slot"
    for (int i = 0; i < 8; i++) {
        block_read(swap, slot + i, buffer);
        // Copy over file contents to the buffer
        memcpy(upage, buffer, BLOCK_SECTOR_SIZE);
        // Write the buffer to swap
        upage += BLOCK_SECTOR_SIZE;
    }
    return;
}

/*
 * Add other functions
 */
