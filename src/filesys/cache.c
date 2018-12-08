#include "cache.h"
#include "filesys/inode.h"

/* File cache table containing all the cache entries */
static struct file_cache_entry[MAX_CACHE_SIZE] file_cache_table;
/* Hash map to allow us to quickly look up cache entries by sector */
//static struct hash file_cache_table_lookup;
/* Global file lock - TODO: replace with a per entry lock */
struct lock file_cache_lock;
/* Index in the table that contains a cache entry */
static block_sector_t clock_hand;

/*! Hash function for the file cache table.
unsigned file_cache_hash (const struct hash_elem *v_, void *aux UNUSED) {
    const struct file_cache_entry *v
        = hash_entry(v_, struct file_cache_entry, elem);
    return hash_int(v->sector);
}
*/

/*! Returns true if entry a precedes entry b
 *  TODO: Consider changing this to compare the time of last use
    so the iteration for eviction makes more sense

bool file_cache_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED) {
    const struct file_cache_entry *a
        = hash_entry (a_, struct file_cache_entry, elem);
    const struct file_cache_entry *b
        = hash_entry (b_, struct file_cache_entry, elem);
    return a->sector < b->sector;
}
*/

/* Initializes the file cache table. */
void file_cache_table_init(void) {
    /* Initialize the file cache table with empty not-in-use blocks */
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        file_cache_table[i].sector = 0;
        file_cache_table[i]->inode = NULL;
        file_cache_table[i]->data = NULL;
        file_cache_table[i].in_use = false;
        file_cache_table[i].accessed = false;
        file_cache_table[i].dirty = false;
        lock_init(&file_cache_table[i].cache_entry_lock);
        lock_init(&file_cache_table[i].evict_lock);
    }
    /* Initialize the lookup hash table */
    //hash_init(file_cache_table_lookup, &file_cache_hash, &file_cache_less, NULL);
    /* Initialize the global lock. */
    lock_init(&file_cache_lock);
    /* Initialize the clock hand to the first element in the cache table */
    clock_hand = 0;
}

/*!
 *  Finds the corresponding file cache entry for a given sector.
 *  If none are found and active == false, returns NULL.
 *  If none are found and active == true, cache entry is loaded from disk
 *  (evicting a block if the cache is full)
 *  TODO: Change this to using a hashmap for lookup
 */
struct file_cache_entry *find_entry(struct inode *inode, bool active) {
    block_sector_t sector = inode_get_inumber(inode);
    struct file_cache_entry *entry = NULL;
    struct file_cache_entry *empty_entry = NULL;
    lock_acquire(&file_cache_lock);
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        if (file_cache_table[i].sector == sector 
            && file_cache_table[i].in_use) {
            entry = &file_cache_table[i];
            break;
        }
        if (empty_entry == NULL && ! file_cache_table[i].in_use) {
            empty_entry = &file_cache_table[i];
        }
    }
    /* Load the entry from disk if entry is not found and active == true */
    if (entry == NULL && active) {
        entry = empty_entry;
        if (! entry) {
            entry = evict_block();
        }
        load_from_disk(entry, inode);
    }
    lock_release(&file_cache_lock);
    return entry;
    //struct file_cache_entry to_find;
    //to_find.sector = sector;
    //struct hash_elem *e = hash_find(file_cache_table, &to_find.elem);
    //return e != NULL ? hash_entry(e, struct file_cache_entry, elem) : NULL;
}

/*!
 *  Load a block from disk to the file buffer cache.
 *  This function should be called upon a page fault to bring
 *  into memory a certain inode block.
 *  Returns true if the data from the file loads properly, else false.
 */
bool load_from_disk(struct file_cache_entry *cache_entry, struct inode *inode) {
    ASSERT(inode != NULL);
    ASSERT(cache_entry != NULL);
    lock_acquire(&cache_entry->evict_lock);
    ASSERT(cache_entry->in_use == false);
    cache_entry->sector = inode->sector;
    cache_entry->inode = inode;
    cache_entry->data = malloc(BLOCK_SECTOR_SIZE);
    off_t size = inode_read_at(cache_entry->inode,
        cache_entry->data, BLOCK_SECTOR_SIZE, 0);
    ASSERT(size == BLOCK_SECTOR_SIZE); // return false
    cache_entry->accessed = false;
    cache_entry->dirty = false;
    lock_release(&cache_entry->evict_lock);
    return true;
}

struct file_cache_entry *evict_block(void) {
    /* Find the cache entry to evict */
    file_cache_entry *to_evict = clock_eviction();
    lock_acquire(&to_evict->evict_lock);
    ASSERT(to_evict != NULL);
    /* Write the data from this entry back to disk if 
     * the block is dirty */
    if (to_evict.dirty == true) {
        off_t size = inode_write_at(to_evict->inode,
            to_evict->data, BLOCK_SECTOR_SIZE, 0); 
        ASSERT(size == BLOCK_SECTOR_SIZE); // return false
    }
    to_evict.sector = 0;
    to_evict->inode = NULL;
    free(to_evict->data);
    to_evict->data = NULL;
    to_evict.in_use = false;
    to_evict.accessed = false;
    to_evict.dirty = false;
    /* Remove this entry from the hash map */
    //hash_delete(&file_cache_table, &to_evict->elem)
    lock_release(&to_evict->evict_lock);
    return to_evict;
}

/*!
 * Uses the clock eviction policy to evict a page.
 */
struct file_cache_entry *clock_eviction(void) {
    struct file_cache_entry *cache_entry = &file_cache_table[clock_hand];
    /* Check the page currently under the clock hand. */
    while (cache_entry.accessed == true) {
        cache_entry.accessed = false;
        clock_hand = (clock_hand + 1) % (MAX_CACHE_SIZE);
        cache_entry = &file_cache_table[clock_hand];
    }
    return cache_entry;
}