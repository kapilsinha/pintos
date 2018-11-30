#include "devices/block.h"
#include "hash.h"

/*!
 * Struct for supplemental page table of this thread.
 * Hash table maps virtual page addresses to the following struct which has
 * supplemental information about the page. */
struct supp_page_table_entry {
    void *page_addr;        // Virtual page address
    bool save_loc;          // 1 if saved to swap and 0 if saved to backing file
    struct block *swap_slot; // Pointer to swap slot
    bool eviction_status;   // 1 if page is evicted, 0 if it is in memory
    struct hash_elem elem;  // Element for hash table
};

void test(void);
