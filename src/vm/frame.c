#include "frame.h"
#include <stdio.h>
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

/* Implementation of frame table allocator. */

/* The frame table contains one entry for each frame that contains a user page.
    Each entry in the frame table contains a pointer to the page, if any, that
    currently occupies it, and other data of your choice. The frame table allows
    Pintos to efficiently implement an eviction policy, by choosing a page to
    evict when no frames are free. */

// TODO: Add lock for frame table
static frame_table_entry *frame_table;
static size_t num_user_pages;

/* Initializes the frame table. Returns 0 if successful, otherwise 1. */
int frame_table_init(size_t user_pages) {
    num_user_pages = user_pages - 1;
    // init_ram_pages is defined in start.S and loader.h
    frame_table = malloc(sizeof(frame_table_entry) * num_user_pages);
    if (!frame_table) {
        PANIC("Failed to allocate frame table!\n");
        return 1;
    }
    // Populate the frame table with physical pages
    for (unsigned int i = 0; i < num_user_pages; i++) {
        frame_table[i].in_use = 0;
        frame_table[i].processes = NULL;
        // Get page from user pool to keep kernel from running out of memory
        frame_table[i].frame = palloc_get_page(PAL_USER | PAL_ASSERT);
        if (frame_table[i].frame == NULL) {
            PANIC("Failed to allocated frame!");
            return 1;
        }
    }
    return 0;
}

/* Returns a pointer to a physical frame from the frame table. */
void *frame_get_page(void) {
    // Loop over frame table to find unused frame
    for (unsigned int i = 0; i < num_user_pages; i++) {
        if (frame_table[i].in_use == 0) {
            frame_table[i].in_use = 1;
            return frame_table[i].frame;
        }
    }
    // We could not find an unused frame, PANIC the kernel
    PANIC("Ran out of frames!");
}

/* Frees a physical frame by marking it as unused. */
void frame_free_page(void *frame UNUSED) {
    // TODO: Might need to also zero out the frame
    return;
}
