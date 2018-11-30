#include "list.h"
#include "threads/synch.h"

/* Entry for the frame table. */
// TODO: get rid of the typedef
struct frame_table_entry {
    int in_use;                 // Whether this frame is being used or not
    struct lock *pin;            // Whether this page is pinned or not
    struct list processes;      // List of processes using this page
    void *frame;                // Pointer to the frame for this entry
};

struct frame_table_entry *frame_table;

void frame_table_init(size_t user_pages);
void *frame_get_page(void);
void frame_free_page(void *frame);
