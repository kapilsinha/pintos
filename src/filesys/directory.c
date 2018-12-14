#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/*! A directory. */
struct dir {
    struct inode *inode;                /*!< Backing store. */
    off_t pos;                          /*!< Current position. */
};

/*! A single directory entry. */
struct dir_entry {
    block_sector_t inode_sector;        /*!< Sector number of header. */
    char name[NAME_MAX + 1];            /*!< Null terminated file name. */
    bool in_use;                        /*!< In use or free? */
};

/*! Return the sector that contains the metadata (inode_disk) for a
 *  given directory. We use the fact that inumber corresponds to a
 *  sector number in Pintos */
block_sector_t get_dir_metadata_sector(struct dir *dir) {
    return inode_get_inumber(dir->inode);
}

/*! Creates a directory with space for ENTRY_CNT entries in the
    given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(struct dir *cur_dir, block_sector_t sector, size_t entry_cnt) {
    return inode_create(get_dir_metadata_sector(cur_dir), sector,
        entry_cnt * sizeof(struct dir_entry), true);
}

/*! Opens and returns the directory for the given INODE, of which
    it takes ownership.  Returns a null pointer on failure. */
struct dir * dir_open(struct inode *inode) {
    struct dir *dir = calloc(1, sizeof(*dir));
    if (inode != NULL && dir != NULL) {
        dir->inode = inode;
        dir->pos = 0;
        return dir;
    }
    else {
        inode_close(inode);
        free(dir);
        return NULL; 
    }
}

/*! Opens the root directory and returns a directory for it.
    Return true if successful, false on failure. */
struct dir * dir_open_root(void) {
    return dir_open(inode_open(ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir * dir_reopen(struct dir *dir) {
    return dir_open(inode_reopen(dir->inode));
}

/*! Destroys DIR and frees associated resources. */
void dir_close(struct dir *dir) {
    if (dir != NULL) {
        inode_close(dir->inode);
        free(dir);
    }
}

/*! Returns the inode encapsulated by DIR. */
struct inode * dir_get_inode(struct dir *dir) {
    return dir->inode;
}

/*! Returns the length of DIR (in bytes). */
int dir_get_length(struct dir *dir) {
    return (int) inode_length(dir->inode);
}

/*! Returns the number of dir_entries in DIR */
int dir_get_num_entries(struct dir *dir) {
    int num_entries = 0;
    struct dir_entry e;
    off_t pos = dir->pos;
    while (inode_read_at(dir->inode, &e, sizeof(e), pos) == sizeof(e)) {
        pos += sizeof(e);
        if (e.in_use) {
            num_entries++;
        }
    }
    return num_entries;
}

/*! Returns the directory containing DIR. */
struct dir *dir_get_parent_dir(struct dir *dir) {
    return dir_open(get_parent_dir_inode(dir->inode));
}

/*! Returns the name of the directory DIR. */
const char *dir_get_name(struct dir *dir) {
    block_sector_t dir_sector = get_dir_metadata_sector(dir);
    struct dir *parent_dir = dir_get_parent_dir(dir);
    struct dir_entry e;
    off_t pos = dir->pos; // TODO: maybe just set to 0?
    char *name = malloc((NAME_MAX + 1) * sizeof(char));
    while (inode_read_at(parent_dir->inode, &e, sizeof(e), pos) == sizeof(e)) {
        pos += sizeof(e);
        if (e.inode_sector == dir_sector) {
            memcpy(name, e.name, (int) strlen(e.name));
            name[(int) strlen(e.name)] = '\0';
            return name;
        }
    }
    name[0] = '\0';
    return name;
}

/*! Searches DIR for a file with the given NAME.
    If successful, returns true, sets *EP to the directory entry
    if EP is non-null, and sets *OFSP to the byte offset of the
    directory entry if OFSP is non-null.
    otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir *dir, const char *name,
                   struct dir_entry *ep, off_t *ofsp) {
    struct dir_entry e;
    size_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
         ofs += sizeof(e)) {
        if (e.in_use && !strcmp(name, e.name)) {
            if (ep != NULL)
                *ep = e;
            if (ofsp != NULL)
                *ofsp = ofs;
            return true;
        }
    }
    return false;
}

/*! Searches DIR for a file with the given NAME and returns true if one exists,
    false otherwise.  On success, sets *INODE to an inode for the file,
    otherwise to a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir *dir, const char *name, struct inode **inode) {
    struct dir_entry e;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    if (lookup(dir, name, &e, NULL))
        *inode = inode_open(e.inode_sector);
    else
        *inode = NULL;

    return *inode != NULL;
}

/*! Adds a file named NAME to DIR, which must not already contain a file by
    that name.  The file's inode is in sector INODE_SECTOR.
    Returns true if successful, false on failure.
    Fails if NAME is invalid (i.e. too long) or a disk or memory
    error occurs. */
bool dir_add(struct dir *dir, const char *name, block_sector_t inode_sector) {
    struct dir_entry e;
    off_t ofs;
    bool success = false;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    /* Check NAME for validity. */
    if (*name == '\0' || strlen(name) > NAME_MAX)
        return false;

    /* Check that NAME is not in use. */
    if (lookup(dir, name, NULL, NULL))
        goto done;

    /* Set OFS to offset of free slot.
       If there are no free slots, then it will be set to the
       current end-of-file.
     
       inode_read_at() will only return a short read at end of file.
       Otherwise, we'd need to verify that we didn't get a short
       read due to something intermittent such as low memory. */
    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
         ofs += sizeof(e)) {
        if (!e.in_use)
            break;
    }

    /* Write slot. */
    e.in_use = true;
    strlcpy(e.name, name, sizeof e.name);
    e.inode_sector = inode_sector;
    success = inode_write_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);

done:
    return success;
}

/*! Removes any entry for NAME in DIR.  Returns true if successful, false on
    failure, which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir *dir, const char *name) {
    struct dir_entry e;
    struct inode *inode = NULL;
    bool success = false;
    off_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    /* Find directory entry. */
    if (!lookup(dir, name, &e, &ofs))
        goto done;

    /* Open inode. */
    inode = inode_open(e.inode_sector);
    if (inode == NULL)
        goto done;

    /* Erase directory entry. */
    e.in_use = false;
    if (inode_write_at(dir->inode, &e, sizeof(e), ofs) != sizeof(e))
        goto done;

    /* Remove inode. */
    inode_remove(inode);
    success = true;

done:
    inode_close(inode);
    return success;
}

/*! Reads the next directory entry in DIR and stores the name in NAME.  Returns
    true if successful, false if the directory contains no more entries. */
bool dir_readdir(struct dir *dir, char name[NAME_MAX + 1]) {
    struct dir_entry e;

    while (inode_read_at(dir->inode, &e, sizeof(e), dir->pos) == sizeof(e)) {
        dir->pos += sizeof(e);
        if (e.in_use) {
            strlcpy(name, e.name, NAME_MAX + 1);
            return true;
        } 
    }
    return false;
}

/*!
 *  Reads in the path and attempts to return the lowest directory.
 *  Returns a short_path struct containing a dir * and a filename *
 *  and is_dir flag. There are three possible outcomes:
 *  1. The path is invalid - dir * and filename * are set to NULL
 *  2. The path is valid and refers to a normal file - dir * is set
 *     to this directory and filename * contains the name of the file
 *  3. The path is valid and refers to a directory - dir * is set to
 *     the directory that the path refers to and filename * contains
 *     the name of this lowest directory
 */
struct short_path *get_dir_from_path(struct dir *cur_dir, const char *path) {
    ASSERT(cur_dir != NULL);
    struct dir *dir;
    struct dir *parent_dir;
    struct inode *inode;
    char *next_dir_name = malloc((NAME_MAX + 1) * sizeof(char));
    int start;
    int end = (int) strlen(path) - 1;
    bool is_dir;

    /* If path starts with '/', the path is absolute so we set the start
     * directory to the root directory.
     * Else the path is relative so we set the start directory to cur_dir,
     * which should be the calling thread's current directory */
    if (path[0] == '/') {
        dir = dir_open_root();
        start = 1;
    }
    else {
        dir = dir_reopen(cur_dir);
        start = 0;
    }
    parent_dir = dir;

    /* Get rid of the trailing '/'s at the end of the path */
    for (int j = (int) strlen(path) - 1; j >= 0; j--) {
        if (path[j] != '/') {
            break;
        }
        end = j - 1;
    }

    /* Iterate over the path from left to right until either the current
     * directory does not exist or we finish iterating */
    for (int i = start; i <= end; i++) {
        parent_dir = dir;
        /* If dir is NULL or was removed, the path is invalid. Return NULL */
        if (! parent_dir || inode_isremoved(parent_dir->inode)) {
            if (parent_dir && parent_dir->inode) {
                dir_close(parent_dir);
            }
            parent_dir = NULL;
            break;
        }
        if (path[start] == '/') {
            start = i + 1;
            continue;
        }
        if (path[i] != '/' && i != end) {
            continue;
        }
        if (i == end) {
            i++;
        }
        /* If the directory name is too large, the directory is invalid */
        if (i - start > NAME_MAX) {
            dir_close(parent_dir);
            parent_dir = NULL;
            break;
        }
        memcpy(next_dir_name, path + start, i - start);
        next_dir_name[i - start] = '\0';
        if (strcmp(next_dir_name, ".") == 0) {
            /* If the next subdirectory is '.', we stay at the cur_dir */
            dir = dir_reopen(cur_dir);
        }
        else if (strcmp(next_dir_name, "..") == 0) {
            /* If the next subdirectory is '..', we go up to parent_dir */
            dir = dir_open(get_parent_dir_inode(dir->inode));
        }
        else {
            /* Else we look for the file name in the current directory */
            dir_lookup(dir, next_dir_name, &inode);
            dir = dir_open(inode);
        }
        /* Reclaim the old directory inode */
        if (i < end) {
            dir_close(parent_dir);
            start = i + 1;
        }
    }
    /* If the parent_dir is valid, determine if the path we seek corresponds
     * to a directory (dir is also valid) or for an ordinary file 
     * (dir is invalid) */
    if (! parent_dir) {
        /* If the containing directory is invalid, set dir to NULL */
        dir = NULL;
        free(next_dir_name);
        next_dir_name = NULL;
        is_dir = false;
    }
    else if (! dir || inode_isremoved(dir->inode) || ! inode_isdir(dir->inode)) {
        /* If the containing directory is valid but the path refers to an
         * ordinary file, set dir to the parent_dir and also return the name
         * of the file */
        dir = parent_dir;
        memcpy(next_dir_name, path + start, (int) strlen(path) - start);
        next_dir_name[(int) strlen(path) - start] = '\0';
        is_dir = false;
    }
    else {
        /* If the containing directory is valid and the path refers to a
         * subdirectory, set dir to this subdirectory and return NULL for
         * the name of the file */
        free(next_dir_name);
        next_dir_name = (char *) dir_get_name(dir);
        is_dir = true;
    }
    struct short_path *sp = malloc(sizeof(struct short_path));
    sp->dir = dir;
    sp->filename = next_dir_name;
    sp->is_dir = is_dir;
    // printf("Is dir?: %d\n", sp->is_dir);
    // printf("Filename: %s\n", sp->filename);
    return sp;
}

void print_absolute_path(struct dir *dir UNUSED) {
    printf("ye\n");
}