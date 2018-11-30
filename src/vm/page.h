#include <debug.h>
#include "devices/block.h"
#include "hash.h"
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

/*!
 *  Type of page source
 */
enum page_source_type {
    PAGE_SOURCE_EXECUTABLE, /* Virtual page loaded in an executable */
    PAGE_SOURCE_MMAP,       /* Virtual page loaded in a memory-mapped file */
    PAGE_SOURCE_STACK       /* Virtual page represents a program stack */
};

/*!
 *  Struct containing the information to write or load from a backing file
 */
struct backing_file {
    struct file *file;        /* file struct for the backing file */
    uint32_t page_data_bytes; /* number of bytes to read/write from/to page */
    uint32_t page_zero_bytes; /* number of bytes to zero out at the end
                                 when reading a file into a page */
};

/*!
 *  Struct for supplemental page table of this thread.
 *  Hash table maps virtual page addresses to the following struct which has
 *  supplemental information about the page.
 */
struct supp_page_table_entry {
    enum page_source_type type; // Type of page source (what the page contains)
    void *page_addr;         // Virtual page address
    bool save_loc;           // 1 to save to swap, 0 to save to backing file
    bool load_loc;           // 1 to load from swap, 0 to load from backing file
    int eviction_status;     // 1 if page is evicted, 0 if it is in memory,
                             // 2 if in the process of evicting
    struct hash_elem elem;   // Element for hash table
    size_t swap_slot;        // Swap slot that contains this page if load_loc=1
    struct backing_file bf;  // Data to read/write to backing file
};

unsigned vaddr_hash (const struct hash_elem *v_, void *aux UNUSED);
bool vaddr_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED);

/* Add a supplemental page table entry for loading an executable */
void supp_add_exec_entry(struct file *f, uint32_t page_data_bytes,
    uint32_t page_zero_bytes, void *page_addr);
