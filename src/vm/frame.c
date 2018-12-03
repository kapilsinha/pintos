#include "frame.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "random.h"
#include "page.h"
#include "swap.h"

/* Implementation of frame table allocator. */

/* The frame table contains one entry for each frame that contains a user page.
    Each entry in the frame table contains a pointer to the page, if any, that
    currently occupies it, and other data of your choice. The frame table allows
    Pintos to efficiently implement an eviction policy, by choosing a page to
    evict when no frames are free. */

static size_t num_user_pages;
static struct lock frame_table_lock;
static struct lock evict_lock;
static int clock_hand;

struct frame_table_entry *clock_eviction(void);

/*!
 * Returns a pointer to the frame table entry struct for this frame. If not
 * found, returns NULL. */
struct frame_table_entry *get_frame_entry(void *frame) {
    // TODO: Maybe find more efficient way of doing this?
    for (unsigned int i = 0; i < num_user_pages; i++) {
        if (frame_table[i].frame == frame) {
            return &frame_table[i];
        }
    }
    return NULL;
}

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
        frame_table[i].page = NULL;
        frame_table[i].t = NULL;
        lock_init(&frame_table[i].pin);
        // Get page from user pool to keep kernel from running out of memory
        frame_table[i].frame = palloc_get_page(PAL_USER | PAL_ASSERT | PAL_ZERO);
        if (frame_table[i].frame == NULL) {
            PANIC("Failed to allocate frame!");
        }
    }
    // Initialize the clock hand to point to the first page
    clock_hand = 0;
    // Initialize the lock
    lock_init(&frame_table_lock);
    lock_init(&evict_lock);
}

/*!
 * Uses the clock eviction policy to evict a page.
 */
struct frame_table_entry *clock_eviction(void) {
    struct frame_table_entry *frame = &frame_table[clock_hand];
    // Check the page currently under the clock hand
    while (pagedir_is_accessed(frame->t->pagedir, frame->page)) {
        pagedir_set_accessed(frame->t->pagedir, frame->page, false);
        clock_hand = (clock_hand + 1) % (num_user_pages);
        frame = &frame_table[clock_hand];
    }
    return frame;
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
    // If no empty frames are found, evict a page
    lock_acquire(&evict_lock);
    struct frame_table_entry *evicted = evict_page();
    lock_release(&evict_lock);
    evicted->in_use = 1;
    return evicted->frame;
}

/* Frees a physical frame by marking it as unused. */
void frame_free_page(void *frame) {
    lock_acquire(&frame_table_lock);
    struct frame_table_entry *entry = get_frame_entry(frame);
    pagedir_clear_page(entry->t->pagedir, entry->page);
    if (entry == NULL) PANIC("Could not find frame to free!");
    entry->in_use = 0;
#ifndef NDEBUG
            memset(frame, 0xcc, PGSIZE);
#endif
    lock_release(&frame_table_lock);
    return;
}

/*!
 * Evicts a single frame from memory and returns a pointer to the frame that was
 * just evicted. Assumes that all frames are being used.
 */
struct frame_table_entry *evict_page(void) {
    // TODO: Implement clock eviction policy
    struct frame_table_entry *rand_frame = clock_eviction();
    struct thread *t = rand_frame->t;
    // Acquire lock before evicting
    lock_acquire(&rand_frame->pin);
    // Get supplemental page table entry
    ASSERT(rand_frame->page != NULL);
    struct supp_page_table_entry *sup_entry = find_entry(rand_frame->page, t);
    if (!sup_entry) PANIC("Didn't find entry in evict_page");
    sup_entry->eviction_status = 2;
    // Determine where to write i.e. swap or disk
    if (sup_entry->save_loc == 1) {// Save to swap
        size_t slot = swap_write(rand_frame->page);
        sup_entry->swap_slot = slot;
        sup_entry->load_loc = 1;
    }
    else {// Save to backing file
        ASSERT(file_write(sup_entry->bf.file, rand_frame->page, PGSIZE) == PGSIZE);
        sup_entry->load_loc = 0;
    }
    // Evict the page and update the frame table entry
    pagedir_clear_page(t->pagedir, rand_frame->page);
    rand_frame->in_use = 0;
    // Mark as evicted
    sup_entry->eviction_status = 1;
    lock_release(&rand_frame->pin);
    // Return the frame address after clearing the frame
    memset(rand_frame->frame, 0, PGSIZE);
    return rand_frame;
}
