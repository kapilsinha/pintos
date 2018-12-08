#include <stdbool.h>
#include <debug.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#define MAX_CACHE_SIZE 64         /* maximum number of blocks in cache */

struct file_cache_entry {
    block_sector_t sector;        /* Equal to inode->sector, key to hash map */
    struct inode *inode;          /* Pointer to the inode */
    void *data;                   /* Cached data contained at the inode
                                   * (originally, contents at inode->sector) */
    bool in_use;                  /* 1 if entry is being used (data is valid),
                                   * else 0 */ 
    bool accessed;                /* 1 if cache has been accessed, else 0 */
    bool dirty;                   /* 1 if cache has been written to, else 0 */
    struct lock cache_entry_lock; /* TODO: change to read/write lock */
    struct lock evict_lock;       /* If in process of evicting, acquire this lock */
};

void file_cache_table_init(void);
struct file_cache_entry *find_file_cache_entry(struct inode *inode, bool active);
bool load_from_disk(struct file_cache_entry *cache_entry, struct inode *inode);

/* Eviction policy */
struct file_cache_entry *evict_block(void);
struct file_cache_entry *file_cache_clock_eviction(void);