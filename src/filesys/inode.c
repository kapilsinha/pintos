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

#define NUM_DIRECT 12
/* Number of block numbers that can fit in one sector. */
#define NUM_SECTORS 128

/*!
 *  On-disk inode.
 *  Must be exactly BLOCK_SECTOR_SIZE bytes long.
 */
struct inode_disk {
    block_sector_t direct[NUM_DIRECT];      /*!< Direct nodes. */
    block_sector_t indirect;                /*!< Indirect nodes. */
    block_sector_t double_indirect;         /*!< Double indirect nodes. */
    off_t length;                           /*!< File size in bytes. */
    unsigned magic;                         /*!< Magic number. */
    uint32_t unused[112];                   /*!< Not used. */
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
};

block_sector_t sector_transform(struct inode *inode, block_sector_t sector);

/*!
 * Transforms a contiguous sector number to the actual sector number since files
 * are not actually contiguous on disk.
 */
block_sector_t sector_transform(struct inode *inode, block_sector_t sector) {
    // This should already be in cache but if it isn't, add it
    struct file_cache_entry *metadata = find_file_cache_entry(inode->sector, true);
    struct inode_disk data = *(struct inode_disk *)metadata->data;
    if (sector < 12) {// This is in one of the direct block
        return data.direct[sector];
    }
    // Single indirect block, we need to read in the array from disk
    else if (sector >= 12 && sector < 140) {
        block_sector_t indirect[128];
        block_read(fs_device, data.indirect, indirect);
        return indirect[sector - 12];
    }
    else if (sector >= 140 && sector < 16384) {
        block_sector_t double_indirect[128];
        block_sector_t sector_indirect[128];
        // First, read in the double indirect sector
        block_read(fs_device, data.double_indirect, double_indirect);
        // Determine which double indirect sector we need
        block_sector_t first_indirect = (sector - 12 - 128) / 128;
        block_sector_t second_indirect = (sector - 12 - 128) % 128;
        // Read in the sector that actually contains the pointers to data
        block_read(fs_device, double_indirect[first_indirect], sector_indirect);
        printf("Returning sector: %lu\n", sector_indirect[second_indirect]);
        return sector_indirect[second_indirect];
    }
    else {
        PANIC("Cannot handle sector number greater than 16384!");
    }
}

/*!
 *  Returns the block device sector that contains byte offset POS
 *  within INODE.
 *  Returns -1 if INODE does not contain data for a byte at offset
 *  POS.
 */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);
    struct file_cache_entry *metadata;
    bool success = false;
    while (! success) {
        metadata = find_file_cache_entry(inode->sector, true);
        rw_read_acquire(&metadata->rw_lock);
        if (verify_cache_entry_sector(metadata, inode->sector)) {
            success = true;
        }
        else {
            rw_read_release(&metadata->rw_lock);
        }
    }
    struct inode_disk data = *((struct inode_disk *) metadata->data);
    int length = data.length;
    rw_read_release(&metadata->rw_lock);
    if (pos / BLOCK_SECTOR_SIZE >= 16384) {
        PANIC("byte_to_sector: Cannot handle sector of this size!");
    }
    if (pos < length)
        return sector_transform(inode, pos / BLOCK_SECTOR_SIZE);
    else
        return -1;
}

/*!
 *  List of open inodes, so that opening a single inode twice
 *  returns the same `struct inode'.
 */
static struct list open_inodes;

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
bool inode_create(block_sector_t sector, off_t length) {
    struct inode_disk *disk_inode = NULL;

    ASSERT(length >= 0);
    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL) {
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;
        disk_inode->indirect = 0;
        disk_inode->double_indirect = 0;
        size_t sectors = bytes_to_sectors(length);
        // Allocate each sector individually
        block_sector_t s_i;
        static char filler[BLOCK_SECTOR_SIZE];
        memset(filler, 0, BLOCK_SECTOR_SIZE);
        block_sector_t indirect_block[NUM_SECTORS];
        block_sector_t double_indirect[NUM_SECTORS];
        // This is the sector in which the table that stores the data sectors is stored
        block_sector_t data_table_sector;
        // This is the sector for the actual data of the file
        block_sector_t file_data_sector;
        // This is the table that stores the sectors of the secondary tables
        block_sector_t table_for_tables[NUM_SECTORS];
        // This is the table that stores the sectors of the data
        block_sector_t table_for_data[NUM_SECTORS];
        size_t i = 0;
        while (i < sectors) {
            if (!free_map_allocate(1, &s_i)) return false;
            if (i < 12) {// This will be one of the direct blocks
                disk_inode->direct[i] = s_i;
                block_write(fs_device, disk_inode->direct[i], filler);
            }
            else if (i >= 12 && i < 140) {// An indirect block
                if (i == 12) {// Need to allocate a sector for the table
                    block_sector_t indirect_sector;
                    if (!free_map_allocate(1, &indirect_sector)) return false;
                    disk_inode->indirect = indirect_sector;
                }
                if (!free_map_allocate(1, &s_i)) return false;
                indirect_block[i - 12] = s_i;
            }
            else if (i >= 140 && i < 16384) {// A doubly indirect block
                printf("Dealing with sector %lu\n", i);
                // Allocate a sector for the table of tables
                if (disk_inode->double_indirect == 0) {
                    if (!free_map_allocate(1, &disk_inode->double_indirect)) return false;
                }
                if (!free_map_allocate(1, &data_table_sector)) return false;
                // Keep looping until we have created all sectors
                printf("Number of double sectors to add: %d\n", sectors);

                printf("In the while loop\n");
                if (!free_map_allocate(1, &file_data_sector)) return false;
                printf("Allocated sector %lu\n", file_data_sector);
                // Fill with zeros
                block_write(fs_device, file_data_sector, filler);
                printf("Index into table for data: %d\n", (i - 12 - 128) % 128);
                table_for_data[(i - 12 - 128) % 128] = file_data_sector;
                // We need to write the table for data every 128 blocks
                if ((i - 12 - 128) % 128 == 0) {
                    printf("Writing to device\n");
                    block_write(fs_device, data_table_sector, table_for_data);
                    //PANIC("AAAAAAAAAAAAAAAAAAAA");
                    table_for_tables[(i - 12 - 128) / 128] = data_table_sector;
                    if (!free_map_allocate(1, &data_table_sector)) return false;
                }
            }
            else {
                PANIC("Cannot handle file of this size!");
            }
            i++;
        }
        // Need to write the indirect table to the sector
        if (disk_inode->indirect != 0) {
            block_write(fs_device, disk_inode->indirect, indirect_block);
        }
        // DEBUGGING CODE
        // printf("First indexing layer array: \n", );
        // for (int i = 0; i < NUM_SECTORS; i++) {
        // }
        // Need to write all the double indirect tables to their sectors
        printf("Right here: Double indirect sector: %lu\n", disk_inode->double_indirect);
        if (disk_inode->double_indirect != 0) {
            block_write(fs_device, disk_inode->double_indirect, table_for_tables);
        }
        block_write(fs_device, sector, disk_inode);
        free(disk_inode);
    }
    return true;
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
            bool success = false;
            struct file_cache_entry *metadata;
            struct inode_disk data;
            while (! success) {
                metadata = find_file_cache_entry(inode->sector, true);
                rw_read_acquire(&metadata->rw_lock);
                if (verify_cache_entry_sector(metadata, inode->sector)) {
                    success = true;
                    data = *((struct inode_disk *) metadata->data);
                }
            }
            // TODO: Not sure if free first then evict or vice versa
            // We have the inode_disk, now we need to free all its sectors

            block_sector_t to_free;
            for (size_t i = 0; i < bytes_to_sectors(data.length); i++) {
                if (i >= 16384) PANIC("Cannot handle sector greater than 16384!");
                to_free = sector_transform(inode, i);
                free_map_release(to_free, 1);
                // Evict the cache entry for this sector
                evict_sector(to_free);
            }
            /* Evict the metadata cache entry */
            rw_read_release(&metadata->rw_lock);
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
        off_t inode_left = inode_length(inode) - offset;
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
    struct file_cache_entry *metadata;
    bool success = false;
    while (! success) {
        metadata = find_file_cache_entry(inode->sector, true);
        rw_read_acquire(&metadata->rw_lock);
        if (verify_cache_entry_sector(metadata, inode->sector)) {
            success = true;
        }
        else {
            rw_read_release(&metadata->rw_lock);
        }
    }
    struct inode_disk data = *((struct inode_disk *) metadata->data);
    int length = data.length;
    rw_read_release(&metadata->rw_lock);
    return length;
}
