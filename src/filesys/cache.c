#include "cache.h"
#include "filesys/filesys.h"
#include <string.h>

/* File cache table containing all the cache entries */
static struct file_cache_entry file_cache_table[MAX_CACHE_SIZE];
/* Global file lock - TODO: replace with a per entry lock */
static struct lock file_cache_lock;
/* Index in the table that contains a cache entry */
static block_sector_t clock_hand;

/* Initializes the file cache table. */
void file_cache_table_init(void) {
    /* Initialize the file cache table with empty not-in-use blocks */
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        file_cache_table[i].sector = 0;
        file_cache_table[i].data = NULL;
        file_cache_table[i].in_use = false;
        file_cache_table[i].accessed = false;
        file_cache_table[i].dirty = false;
        lock_init(&file_cache_table[i].cache_entry_lock);
        lock_init(&file_cache_table[i].evict_lock);
    }
    /* Initialize the global lock. */
    lock_init(&file_cache_lock);
    //printf("Initialized cache lock\n");
    /* Initialize the clock hand to the first element in the cache table */
    clock_hand = 0;
}

/*!
 *  Gets an unused cache entry, either finding one or evicting one
 */
struct file_cache_entry *get_unused_cache_entry(void) {
    struct file_cache_entry *entry = NULL;
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        if (! file_cache_table[i].in_use) {
            entry = &file_cache_table[i];
            break;
        }
    }
    if (! entry) {
        entry = evict_block(file_cache_clock_eviction());
    }
    return entry;
}

/*!
 *  Finds the corresponding file cache entry for a given sector.
 *  If none are found and active == false, returns NULL.
 *  If none are found and active == true, cache entry is loaded from disk
 *  (evicting a block if the cache is full)
 *  TODO: Change this to using a hashmap for lookup
 */
struct file_cache_entry *find_file_cache_entry(block_sector_t sector,
    bool active) {
    //printf("FIND FILE CACHE ENTRY\n");
    /* File cache entry that will be returned */
    struct file_cache_entry *entry = NULL;
    // TODO: DON'T USE A GLOBAL LOCK!!!
    lock_acquire(&file_cache_lock);
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        if (file_cache_table[i].sector == sector 
            && file_cache_table[i].in_use) {
            entry = &file_cache_table[i];
            //printf("Found entry in cache\n");
            break;
        }
    }
    /* Load the entry from disk if entry is not found and active == true */
    if (entry == NULL && active) {
        entry = get_unused_cache_entry();
        load_from_disk(entry, sector);
        //printf("Loaded from disk\n");
    }
    //debug_word_dump(entry->data, BLOCK_SECTOR_SIZE);
    //printf("FOUND FILE CACHE ENTRY\n");
    lock_release(&file_cache_lock);
    return entry;
}

/*!
 *  Reads SIZE bytes from cached SECTOR (starting at OFFSET) into BUFFER
 */
void file_cache_read(block_sector_t sector, void *buffer, off_t size, off_t offset) {
    struct file_cache_entry *cache_entry = find_file_cache_entry(sector, true);
    memcpy(buffer, cache_entry->data + offset, size);
    cache_entry->accessed = true;
    cache_entry->dirty = true;
}

/*!
 *  Writes SIZE bytes from BUFFER into cached SECTOR (starting at OFFSET)
 */
void file_cache_write(block_sector_t sector, void *buffer, off_t size, off_t offset) {
    struct file_cache_entry *cache_entry = find_file_cache_entry(sector, true);
    memcpy(cache_entry->data + offset, buffer, size);
    cache_entry->accessed = true;
    cache_entry->dirty = true;
}

/*!
 *  Load a block from disk to the file buffer cache, where the
 *  element in the cache table we write to is cache_entry.
 *  Returns true if the data from the file loads properly, else false.
 */
bool load_from_disk(struct file_cache_entry *cache_entry,
    block_sector_t sector) {
    ASSERT(cache_entry != NULL);
    lock_acquire(&cache_entry->evict_lock);
    ASSERT(cache_entry->in_use == false);
    cache_entry->sector = sector;
    cache_entry->data = malloc(BLOCK_SECTOR_SIZE);
    block_read(fs_device, cache_entry->sector, cache_entry->data);
    cache_entry->in_use = true;
    cache_entry->accessed = false;
    cache_entry->dirty = false;
    lock_release(&cache_entry->evict_lock);
    return true;
}

/*! 
 *  Evicts the to_evict file_cache_entry
 */
struct file_cache_entry *evict_block(struct file_cache_entry *to_evict) {
    //printf("EVICTING SOMETHING\n");
    lock_acquire(&to_evict->evict_lock);
    ASSERT(to_evict != NULL);
    /* Write the data from this entry back to disk if the block is dirty */
    if (to_evict->dirty) {
        block_write(fs_device, to_evict->sector, to_evict->data);
    }
    to_evict->sector = 0;
    free(to_evict->data);
    to_evict->data = NULL;
    to_evict->in_use = false;
    to_evict->accessed = false;
    to_evict->dirty = false;
    lock_release(&to_evict->evict_lock);
    return to_evict;
}

/*!
 * Uses the clock eviction policy to find a cache entry to evict.
 */
struct file_cache_entry *file_cache_clock_eviction(void) {
    struct file_cache_entry *cache_entry = &file_cache_table[clock_hand];
    /* Check the page currently under the clock hand. */
    while (cache_entry->accessed == true) {
        cache_entry->accessed = false;
        clock_hand = (clock_hand + 1) % (MAX_CACHE_SIZE);
        cache_entry = &file_cache_table[clock_hand];
    }
    return cache_entry;
}