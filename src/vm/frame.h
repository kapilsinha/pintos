#include "list.h"

/* Entry for the frame table. */
typedef struct {
    int in_use;                 // Whether this frame is being used or not
    struct list *processes;      // List of processes using this page
    void *frame;             // Pointer to the frame for this entry
} frame_table_entry;

int frame_table_init(void);
void *get_frame(void);
void free_frame(void *frame);
