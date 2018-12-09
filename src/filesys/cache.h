#include <stdbool.h>
#include <stdio.h>
#include <debug.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "lib/debug.h"

#define MAX_CACHE_SIZE 64         /* maximum number of blocks in cache */

struct file_cache_entry {
    /* Sector contains metadata if the sector holds an inode_disk, and
     * contains file data if the sector holds a data (file) sector */
    block_sector_t sector;        /* Sector whose data we hold, key to hash map */
    void *data;                   /* Cached data contained at the inode
                                   * (originally, contents at inode->sector) */
    bool in_use;                  /* 1 if entry is being used (data is valid),
                                   * else 0 */ 
    bool accessed;                /* 1 if data has been accessed, else 0 */
    bool dirty;                   /* 1 if data has been written to, else 0 */
    struct lock cache_entry_lock; /* TODO: change to read/write lock */
    struct lock evict_lock;       /* If in process of evicting, acquire this lock */
};

void file_cache_table_init(void);

struct file_cache_entry *get_unused_cache_entry(void);
struct file_cache_entry *find_file_cache_entry(block_sector_t sector, bool active);
void file_cache_read(block_sector_t sector, void *buffer, off_t size, off_t offset);
void file_cache_write(block_sector_t sector, void *buffer, off_t size, off_t offset);

bool load_from_disk(struct file_cache_entry *cache_entry, block_sector_t sector);

/* Eviction policy */
struct file_cache_entry *evict_block(struct file_cache_entry *to_evict);
struct file_cache_entry *file_cache_clock_eviction(void);