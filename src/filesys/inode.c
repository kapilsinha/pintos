#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/*! Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct blocks. */
#define NUM_DIRECT 12
/* Number of block numbers that can fit in one sector. */
#define NUM_SECTORS 128
/* Maximum number of sectors for a file. */
#define MAX_SECTORS 16524
/* Number of sectors before we need to use a double indirect sector. */
#define NUM_DOUBLE 140
/* Size of the block_sector_t type. */
#define SEC_SIZE sizeof(block_sector_t)

/*!
 *  On-disk inode.
 *  Must be exactly BLOCK_SECTOR_SIZE bytes long.
 */
struct inode_disk {
    block_sector_t direct[NUM_DIRECT];      /*!< Direct nodes. */
    block_sector_t indirect;                /*!< Indirect nodes. */
    block_sector_t double_indirect;         /*!< Double indirect nodes. */
    unsigned is_dir;                        /*!< 1 if dir, 0 if file. */
    block_sector_t parent_dir_sector;       /*!< Parent dir's inode->sector */
    off_t length;                           /*!< File size in bytes. */
    unsigned magic;                         /*!< Magic number. */
    uint32_t unused[110];                   /*!< Not used. */
};

/*!
 *  Returns the number of sectors to allocate for an inode SIZE
 *  bytes long.
 */
static inline size_t bytes_to_sectors(off_t size) {
    return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/*! In-memory inode. */
struct inode {
    struct list_elem elem;            /*!< Element in inode list. */
    block_sector_t sector;            /*!< Sector number of disk location. */
    int open_cnt;                     /*!< Number of openers. */
    bool removed;                     /*!< True if deleted, false otherwise. */
    int deny_write_cnt;               /*!< 0: writes ok, >0: deny writes. */
    struct lock extension_lock;       /*!< Lock used for extending the file. */
};

block_sector_t sector_transform(struct inode *inode, block_sector_t sector);
struct file_cache_entry *get_metadata(block_sector_t sector);
bool inode_extend(struct inode_disk *disk_inode,
    size_t curr_sectors, off_t length);

/*!
 *  List of open inodes, so that opening a single inode twice
 *  returns the same `struct inode'.
 */
static struct list open_inodes;

/*!
 * Gets the file cache entry metadata given a sector.
 */
struct file_cache_entry *get_metadata(block_sector_t sector) {
    struct file_cache_entry *metadata;
    bool success = false;
    while (! success) {
        metadata = find_file_cache_entry(sector, true);
        rw_read_acquire(&metadata->rw_lock);
        if (verify_cache_entry_sector(metadata, sector)) {
            success = true;
        }
        else {
            rw_read_release(&metadata->rw_lock);
        }
    }

    return metadata;
}

/*!
 * Transforms a contiguous sector number to the actual sector number since
 * files are not actually contiguous on disk.
 */
block_sector_t sector_transform(struct inode *inode, block_sector_t sector) {
    /* This should already be in cache but if it isn't, add it */
    struct file_cache_entry *metadata = get_metadata(inode->sector);
    rw_read_release(&metadata->rw_lock); /* Releease the lock */
    struct inode_disk data = *(struct inode_disk *)metadata->data;
    rw_read_release(&metadata->rw_lock);
    if (sector < NUM_DIRECT) { /* This is in one of the direct blocks */
        return data.direct[sector];
    }
    /* Single indirect block, we need to read in the array from disk */
    else if (sector >= NUM_DIRECT && sector < NUM_DOUBLE) {
        block_sector_t indirect;
        file_cache_read(data.indirect, &indirect, SEC_SIZE,
            (sector - NUM_DIRECT) * SEC_SIZE);
        return indirect;
    }
    else if (sector >= NUM_DOUBLE && sector < MAX_SECTORS) {
        block_sector_t double_indirect;
        block_sector_t sector_indirect;
        /* First, read in the double indirect sector */
        file_cache_read(data.double_indirect, &double_indirect,
            sizeof(block_sector_t),
            (sector - NUM_DOUBLE) / NUM_SECTORS * SEC_SIZE);
        /* Read in the sector that actually contains the pointers to data */
        file_cache_read(double_indirect, &sector_indirect,
            sizeof(block_sector_t),
            (sector - NUM_DOUBLE) % NUM_SECTORS * SEC_SIZE);
        return sector_indirect;
    }

    else {
        PANIC("Cannot handle sector number greater than MAX_SECTORS!");
    }
}

/*!
 *  Returns the block device sector that contains byte offset POS
 *  within INODE.
 *  Returns -1 if INODE does not contain data for a byte at offset
 *  POS.
 */
static block_sector_t byte_to_sector(struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);
    if (pos / BLOCK_SECTOR_SIZE >= MAX_SECTORS) {
        PANIC("byte_to_sector: Cannot handle sector of this size!");
    }
    if (pos < inode_length(inode)) {
        /* Add sector to read-ahead list */
        // if (pos / BLOCK_SECTOR_SIZE < bytes_to_sectors(inode_length(inode))) {
        //     cache_read_add(sector_transform(inode,
        //         pos / BLOCK_SECTOR_SIZE + 1));
        // }
        return sector_transform(inode, pos / BLOCK_SECTOR_SIZE);
    }
    else
        return -1;
}


/*!
 * This function extends the length of an inode located in sector with
 * a total of curr_sectors so that it can fit length bytes. Returns true if
 * successful, false othewise.
 */
bool inode_extend(struct inode_disk *disk_inode, size_t curr_sectors,
    off_t length) {
    size_t sectors = bytes_to_sectors(length);
    /* If necessary, allocate a sector for the single indirect table */
    if (sectors >= NUM_DIRECT && disk_inode->indirect == 0) {
        if (!free_map_allocate(1, &disk_inode->indirect)) return false;
    }
    if (sectors >= NUM_DOUBLE && disk_inode->double_indirect == 0) {
        if (!free_map_allocate(1, &disk_inode->double_indirect)) return false;
    }
    /* Zeroes to fill in the sectors */
    char *zeroes = malloc(BLOCK_SECTOR_SIZE);
    memset(zeroes, 0, BLOCK_SECTOR_SIZE);
    /* Sector in which the table that stores the data sectors is stored */
    block_sector_t data_table_sector;
    /* This is the sector for the actual data of the file */
    block_sector_t file_data_sector;
    size_t i = curr_sectors;
    while (i < sectors) {
        if (!free_map_allocate(1, &file_data_sector)) return false;
        if (i < NUM_DIRECT) { /* This will be one of the direct blocks */
            disk_inode->direct[i] = file_data_sector;
        }
        else if (i >= NUM_DIRECT && i < NUM_DOUBLE) { /* An indirect block */
            file_cache_write(disk_inode->indirect, &file_data_sector,
                SEC_SIZE, (i - NUM_DIRECT) * SEC_SIZE);
        }
        /* A double indirect block */
        else if (i >= NUM_DOUBLE && i < MAX_SECTORS) {
            /* We need to allocate a new table sector */
            if ((i - NUM_DOUBLE) % NUM_SECTORS == 0) {
                if (!free_map_allocate(1, &data_table_sector)) return false;
                /* Write this new table sector to the primary table */
                file_cache_write(disk_inode->double_indirect,
                    &data_table_sector, SEC_SIZE,
                    (i - NUM_DOUBLE) / NUM_SECTORS * SEC_SIZE);
            }
            file_cache_read(disk_inode->double_indirect, &data_table_sector,
                SEC_SIZE, (i - NUM_DOUBLE) / NUM_SECTORS * SEC_SIZE);
            file_cache_write(data_table_sector, &file_data_sector, SEC_SIZE,
                ((i - NUM_DOUBLE) % NUM_SECTORS) * SEC_SIZE);
        }
        else {
            PANIC("Cannot handle file of this size!");
        }
        i++;
    }
    /* Free memory */
    free(zeroes);
    return true;
}

/*! Initializes the inode module. */
void inode_init(void) {
    list_init(&open_inodes);
}

/*!
 *  Initializes an inode with LENGTH bytes of data and
 *  writes the new inode to sector SECTOR on the file system
 *  device.
 *  Returns true if successful.
 *  Returns false if memory or disk allocation fails.
 */
bool inode_create(block_sector_t parent_dir_sector, block_sector_t sector,
    off_t length, bool is_dir) {
    struct inode_disk *disk_inode = NULL;

    ASSERT(length >= 0);
    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode == NULL) return false;
    memset(disk_inode->direct, 0, 12 * SEC_SIZE);
    disk_inode->indirect = 0;
    disk_inode->double_indirect = 0;
    disk_inode->is_dir = is_dir ? 1 : 0;
    disk_inode->parent_dir_sector = parent_dir_sector;
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    bool ret = inode_extend(disk_inode, 0, length);
    if (!ret) return false;
    file_cache_write(sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
    free(disk_inode);
    return ret;
}

/*!
 *  Reads an inode from SECTOR
 *  and returns a `struct inode' that contains it.
 *  Returns a null pointer if memory allocation fails.
 */
struct inode * inode_open(block_sector_t sector) {
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector) {
            inode_reopen(inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL)
        return NULL;

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    lock_init(&inode->extension_lock);
    return inode;
}

/*! Reopens and returns INODE. */
struct inode * inode_reopen(struct inode *inode) {
    if (inode != NULL)
        inode->open_cnt++;
    return inode;
}

/*! Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode *inode) {
    return inode->sector;
}

/*!
 *  Closes INODE and writes it to disk.
 *  If this was the last reference to INODE, frees its memory.
 *  If INODE was also a removed inode, frees its blocks.
 */
void inode_close(struct inode *inode) {
    /* Ignore null pointer. */
    if (inode == NULL)
        return;

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed) {
            free_map_release(inode->sector, 1);

            /* We have the inode_disk, now we need to free all its sectors */
            block_sector_t to_free;
            for (size_t i = 0; i < bytes_to_sectors(inode_length(inode));
                i++) {
                if (i >= MAX_SECTORS)
                    PANIC("Cannot handle sector greater than MAX_SECTORS!");
                to_free = sector_transform(inode, i);
                free_map_release(to_free, 1);
                /* Evict the cache entry for this sector */
                evict_sector(to_free);
            }
            /* Evict the metadata cache entry */
            evict_sector(inode->sector);
        }
        free(inode);
    }
}

/*!
 *  Marks INODE to be deleted when it is closed by the last caller who
 *  has it open.
 */
void inode_remove(struct inode *inode) {
    ASSERT(inode != NULL);
    inode->removed = true;
}

/*!
 *  Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 *  Returns the number of bytes actually read, which may be less
 *  than SIZE if an error occurs or end of file is reached.
 */
off_t inode_read_at(struct inode *inode, void *buffer_,
    off_t size, off_t offset) {
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;

    while (size > 0) {

        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector (inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        lock_acquire(&inode->extension_lock);
        off_t inode_left = inode_length(inode) - offset;
        lock_release(&inode->extension_lock);
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        /* Read from our cached sector. Note it may vary from what is in disk
         * since data hasn't been written back */
        file_cache_read(sector_idx, buffer + bytes_read,
            chunk_size, sector_ofs);

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }

    return bytes_read;
}

/*!
 *  Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 *  Returns the number of bytes actually written, which may be
 *  less than SIZE if end of file is reached or an error occurs.
 *  (Normally a write at end of file would extend the inode, but
 *  growth is not yet implemented.)
 */
off_t inode_write_at(struct inode *inode, const void *buffer_,
    off_t size, off_t offset) {
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;

    if (inode->deny_write_cnt)
        return 0;

    /* If we are trying to extend the file. */
    lock_acquire(&inode->extension_lock);
    off_t curr_length = inode_length(inode);
    if (offset + size > curr_length) {
        /* Get the disk_inode for this sector */
        struct file_cache_entry *metadata = get_metadata(inode->sector);
        struct inode_disk *disk_inode = (struct inode_disk *) metadata->data;
        /* Allocate sectors as necessary */
        size_t num_current_sectors = bytes_to_sectors(curr_length);
        if (!inode_extend(disk_inode, num_current_sectors,
            offset + size)) return false;
        disk_inode->length += offset + size - curr_length;
        rw_read_release(&metadata->rw_lock);
        file_cache_write(inode->sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
    }
    lock_release(&inode->extension_lock);

    while (size > 0) {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        /* Write directly to our cached sector */
        file_cache_write(sector_idx, (void *) buffer + bytes_written,
            chunk_size, sector_ofs);

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }

    return bytes_written;
}

/*!
 *  Disables writes to INODE.
 *  May be called at most once per inode opener.
 */
void inode_deny_write (struct inode *inode) {
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/*!
 *  Re-enables writes to INODE.
 *  Must be called once by each inode opener who has called
 *  inode_deny_write() on the inode, before closing the inode.
 */
void inode_allow_write (struct inode *inode) {
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/*! Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode) {
    struct file_cache_entry *metadata = get_metadata(inode->sector);
    struct inode_disk data = *((struct inode_disk *) metadata->data);
    int length = data.length;
    rw_read_release(&metadata->rw_lock);
    return length;
}

/*! Opens the argument INODE's parent directory inode */
struct inode *get_parent_dir_inode(struct inode *inode) {
    struct file_cache_entry *metadata = get_metadata(inode->sector);
    struct inode_disk data = *((struct inode_disk *) metadata->data);
    block_sector_t parent_dir_sector = data.parent_dir_sector;
    rw_read_release(&metadata->rw_lock);
    return inode_open(parent_dir_sector);
}

/*! Returns true if the directory has been removed,
 *  false otherwise. Simply checks the inode removed flag
 */
bool inode_isremoved(struct inode *inode) {
    return inode->removed;
}

/*! Returns true if inode represents a directory,
 *  false if inode represents an ordinary file
 */
bool inode_isdir(struct inode *inode) {
    struct file_cache_entry *metadata = get_metadata(inode->sector);
    struct inode_disk data = *(struct inode_disk *)metadata->data;
    bool ret = (data.is_dir == 1);
    rw_read_release(&metadata->rw_lock);
    return ret;
}
