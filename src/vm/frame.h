#include "list.h"
#include "threads/synch.h"

/*! Entry for the frame table. */
struct frame_table_entry {
    int in_use;                 // Whether this frame is being used or not
    struct lock *pin;           // Whether this page is pinned or not
    void *frame;                // Pointer to the frame for this entry
    void *page;                 // Pointer to the page for this entry, if used
    struct thread *t;           // Thread that is currently using this frame
};

struct frame_table_entry *frame_table;

void frame_table_init(size_t user_pages);
void *frame_get_page(void);
void frame_free_page(void *frame);
