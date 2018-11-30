#include "frame.h"
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Implementation of frame table allocator. */

/* The frame table contains one entry for each frame that contains a user page.
    Each entry in the frame table contains a pointer to the page, if any, that
    currently occupies it, and other data of your choice. The frame table allows
    Pintos to efficiently implement an eviction policy, by choosing a page to
    evict when no frames are free. */

static size_t num_user_pages;
static struct lock frame_table_lock;

/* Initializes the frame table. */
void frame_table_init(size_t user_pages) {
    num_user_pages = user_pages - 1;
    // init_ram_pages is defined in start.S and loader.h
    frame_table = malloc(sizeof(struct frame_table_entry) * num_user_pages);
    if (!frame_table) {
        PANIC("Failed to allocate frame table!\n");
    }
    // Populate the frame table with physical pages
    for (unsigned int i = 0; i < num_user_pages; i++) {
        frame_table[i].in_use = 0;
        list_init(&frame_table[i].processes);
        // Get page from user pool to keep kernel from running out of memory
        frame_table[i].frame = palloc_get_page(PAL_USER | PAL_ASSERT | PAL_ZERO);
        if (frame_table[i].frame == NULL) {
            PANIC("Failed to allocate frame!");
        }
    }
    // Initialize the lock
    lock_init(&frame_table_lock);
}

/* Returns a pointer to a physical frame from the frame table. */
void *frame_get_page(void) {
    lock_acquire(&frame_table_lock);
    // Loop over frame table to find unused frame
    for (unsigned int i = 0; i < num_user_pages; i++) {
        if (frame_table[i].in_use == 0) {
            frame_table[i].in_use = 1;
            lock_release(&frame_table_lock);
            return frame_table[i].frame;
        }
    }
    lock_release(&frame_table_lock);
    // We could not find an unused frame, PANIC the kernel
    PANIC("Ran out of frames!");
}

/* Frees a physical frame by marking it as unused. */
void frame_free_page(void *frame) {
    // TODO: Do pointer arithmetic instead of looping
    lock_acquire(&frame_table_lock);
    // Loop over frame table to find this frame frame
    for (unsigned int i = 0; i < num_user_pages; i++) {
        if (frame_table[i].frame == frame) {
            frame_table[i].in_use = 0;
#ifndef NDEBUG
            memset(frame, 0xcc, PGSIZE);
#endif
            break;
        }
    }
    lock_release(&frame_table_lock);
    return;
}
