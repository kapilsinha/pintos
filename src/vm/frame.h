#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "list.h"
#include "threads/synch.h"

/*! Entry for the frame table. */
struct frame_table_entry {
    void *frame;                // Pointer to the frame for this entry
    void *page;                 // Pointer to the page for this entry, if used
    int in_use;                 // Whether this frame is being used or not
    struct lock pin;            // Whether this page is pinned or not
    struct thread *t;           // Thread that is currently using this frame
};

struct frame_table_entry *frame_table;

struct frame_table_entry *get_frame_entry(void *frame);
void update_frame_struct(struct frame_table_entry *entry, uint8_t *upage,
    struct thread *t);
void frame_clear_access_bits(void);

void frame_table_init(size_t user_pages);
void *frame_get_page(void);
void frame_free_page(void *frame);

struct frame_table_entry *evict_page(void);

#endif /* vm/frame.h */
