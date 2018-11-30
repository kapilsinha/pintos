#include "devices/block.h"
#include "hash.h"
#include "filesys/file.h"

/*!
 *  Struct containing the information to write or load from a backing file
 */
struct backing_file {
    struct file *file;
    uint32_t read_bytes;
    uint32_t zero_bytes;
};

/*!
 *  Struct for supplemental page table of this thread.
 *  Hash table maps virtual page addresses to the following struct which has
 *  supplemental information about the page.
 */
struct supp_page_table_entry {
    void *page_addr;         // Virtual page address
    bool save_loc;           // 1 to save to swap, 0 to save to backing file
    bool load_loc;           // 1 to load from swap, 0 to load to backing file
    int eviction_status;     // 1 if page is evicted, 0 if it is in memory,
                             // -1 if in the process of evicting
    struct hash_elem elem;   // Element for hash table
    size_t swap_slot;        // Swap slot that contains this page if load_loc=1
    struct backing_file bf;  // Data to read/write to backing file
};

void test(void);
