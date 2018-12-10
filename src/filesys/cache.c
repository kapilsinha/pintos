#include "cache.h"
#include "filesys/filesys.h"
#include <string.h>

/* File cache table containing all the cache entries */
static struct file_cache_entry file_cache_table[MAX_CACHE_SIZE];
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
        rw_lock_init(&file_cache_table[i].rw_lock);
        lock_init(&file_cache_table[i].evict_lock);
    }
    /* Initialize the clock hand to the first element in the cache table */
    clock_hand = 0;
}

/*!
 *  Verifies that the cache entry is in use and of sector SECTOR.
 *  This function should be called after acquiring the writer's lock to
 *  ensure that the cache entry does not change.
 */
bool verify_cache_entry_sector(struct file_cache_entry *entry,
    block_sector_t sector) {
    return (entry->in_use && entry->sector == sector);
}

/*!
 *  Gets an unused cache entry, either finding one or evicting one
 *  Other than the block eviction, this function does not use locks,
 *  so you must verify that the return cache entry is indeed not in use
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
 *  You must verify that the return cache entry is indeed the sector you
 *  requested, since the cache entry may have been changed.
 */
struct file_cache_entry *find_file_cache_entry(block_sector_t sector,
    bool active) {
    //printf("FIND FILE CACHE ENTRY\n");
    /* File cache entry that will be returned */
    struct file_cache_entry *entry = NULL;
    // TODO: DON'T USE A GLOBAL LOCK!!!
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        if (file_cache_table[i].sector == sector 
            && file_cache_table[i].in_use) {
            entry = &file_cache_table[i];
            //printf("Found entry in cache\n");
            break;
        }
    }
    /* Load the entry from disk if entry is not found and active == true */
    bool success = false;
    if (entry == NULL && active) {
        /* Continue to attempt to load the cache entry from disk into an
         * available sector until we successfully do so */
        //printf("Stuck at find_file_cache_entry?\n");
        while (! success) {
            entry = get_unused_cache_entry();
            success = load_from_disk(entry, sector);
            //printf("Loaded from disk\n");
        }
        //printf("Not stuck at find_file_cache_entry\n");
    }
    //debug_word_dump(entry->data, BLOCK_SECTOR_SIZE);
    //printf("FOUND FILE CACHE ENTRY\n");
    return entry;
}

/*!
 *  Reads SIZE bytes from cached SECTOR (starting at OFFSET) into BUFFER
 */
void file_cache_read(block_sector_t sector, void *buffer,
    off_t size, off_t offset) {
    struct file_cache_entry *cache_entry;
    bool success = false;
    //printf("Stuck at file_cache_read?\n");
    while (! success) {
        cache_entry = find_file_cache_entry(sector, true);
        rw_read_acquire(&cache_entry->rw_lock);
        if (verify_cache_entry_sector(cache_entry, sector)) {
            success = true;
        }
        else {
            rw_read_release(&cache_entry->rw_lock);
        }
    }
    //printf("Not stuck at file_cache_read\n");
    memcpy(buffer, cache_entry->data + offset, size);
    cache_entry->accessed = true;
    rw_read_release(&cache_entry->rw_lock);
}

/*!
 *  Writes SIZE bytes from BUFFER into cached SECTOR (starting at OFFSET)
 */
void file_cache_write(block_sector_t sector, void *buffer,
    off_t size, off_t offset) {
    struct file_cache_entry *cache_entry;
    bool success = false;
    //printf("Stuck at file_cache_write?\n");
    while (! success) {
        cache_entry = find_file_cache_entry(sector, true);
        rw_write_acquire(&cache_entry->rw_lock);
        if (verify_cache_entry_sector(cache_entry, sector)) {
            success = true;
        }
        else {
            rw_write_release(&cache_entry->rw_lock);
        }
    }
    //printf("Not stuck at file_cache_write\n");
    memcpy(cache_entry->data + offset, buffer, size);
    cache_entry->accessed = true;
    cache_entry->dirty = true;
    rw_write_release(&cache_entry->rw_lock);
}

/*!
 *  Loads a block from disk to the file buffer cache, where the element
 *  in the cache table we write to is cache_entry. The writer's lock is
 *  acquired to ensure that we have exclusive access to the cache entry.
 *  Returns true if the data from the file loads properly.
 *  Returns false if the cache entry is in use
 *  (in which case we DO NOT load the file).
 */
bool load_from_disk(struct file_cache_entry *cache_entry,
    block_sector_t sector) {
    ASSERT(cache_entry != NULL);
    lock_acquire(&cache_entry->evict_lock);
    //rw_write_acquire(&cache_entry->rw_lock);
    if (cache_entry->in_use) {
        //rw_write_release(&cache_entry->rw_lock);
        lock_release(&cache_entry->evict_lock);
        return false;
    }
    ASSERT(! cache_entry->in_use);
    cache_entry->sector = sector;
    cache_entry->data = malloc(BLOCK_SECTOR_SIZE);
    block_read(fs_device, cache_entry->sector, cache_entry->data);
    cache_entry->in_use = true;
    cache_entry->accessed = false;
    cache_entry->dirty = false;
    //rw_write_release(&cache_entry->rw_lock);
    lock_release(&cache_entry->evict_lock);
    return true;
}

/*! 
 *  Evicts the to_evict file_cache_entry. If the cache entry is dirty,
 *  it is written back to disk. The writer's lock is acquired prior to 
 *  eviction to ensure we hav exclusive access to the cache entry.
 *  The cache entry being evicted must be in use (otherwise there is no
 *  need to evict it).
 */
struct file_cache_entry *evict_block(struct file_cache_entry *to_evict) {
    //printf("EVICTING SOMETHING\n");
    lock_acquire(&to_evict->evict_lock);
    //rw_write_acquire(&to_evict->rw_lock);
    ASSERT(to_evict != NULL);
    ASSERT(to_evict->in_use);
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
    //rw_write_release(&to_evict->rw_lock);
    lock_release(&to_evict->evict_lock);
    return to_evict;
}

/*!
 *  Evicts the cache entry that contains sector SECTOR. Equivalent to
 *  evict_block, but ensures that the proper sector is being evicted.
 *  Returns NULL if the sector we are looking for is not in the cache
 *  Otherwise returns the cache entry that is evicted
 */
struct file_cache_entry *evict_sector(block_sector_t sector) {
    struct file_cache_entry *to_evict;
    bool success = false;
    //printf("Stuck at evict_sector?\n");
    while (! success) {
        to_evict = find_file_cache_entry(sector, false);
        if (! to_evict) {
            return NULL;
        }
        rw_write_acquire(&to_evict->rw_lock);
        if (verify_cache_entry_sector(to_evict, sector)) {
            success = true;
        }
        else {
            rw_write_release(&to_evict->rw_lock);
        }
    }
    //printf("Not stuck at evict_sector\n");
    ASSERT(to_evict != NULL);
    ASSERT(to_evict->in_use);
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
    rw_write_release(&to_evict->rw_lock);
    return to_evict;
}

/*!
 *  Uses the clock eviction policy to find a cache entry to evict.
 *  This function does not use locks, so it is possible that the
 *  cache entries (including their accessed bits) change as we loop
 *  over them. This should not introduce concurrency issues, however,
 *  since this function simply returns a potential entry to evict.
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