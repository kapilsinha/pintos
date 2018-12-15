#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"

/*! Maximum length of a file name component.
    This is the traditional UNIX maximum length.
    After directories are implemented, this maximum length may be
    retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;
struct dir;

struct short_path {
	struct dir *dir;	  /* Directory containing the file */
	const char *filename; /* Name of the file the path refers to */
	bool is_dir; 		  /* True if filename is a dir,
							 false if it is an ordinary file */
};

block_sector_t get_dir_metadata_sector(struct dir *dir);

/* Opening and closing directories. */
bool dir_create(struct dir *cur_dir, block_sector_t sector, size_t entry_cnt);
struct dir *dir_open(struct inode *);
struct dir *dir_open_root(void);
struct dir *dir_reopen(struct dir *);
void dir_close(struct dir *);

struct inode *dir_get_inode(struct dir *);
int dir_get_length(struct dir *);
int dir_get_num_entries(struct dir *);
struct dir *dir_get_parent_dir(struct dir *);
const char *dir_get_name(struct dir *);

/* Reading and writing. */
bool dir_lookup(const struct dir *, const char *name, struct inode **);
bool dir_add(struct dir *, const char *name, block_sector_t);
bool dir_remove(struct dir *, const char *name);
bool dir_readdir(struct dir *, char name[NAME_MAX + 1]);

/* Processing a directory path */
struct short_path *get_dir_from_path(struct dir *cur_dir, const char *path);
void print_absolute_path(struct dir *dir);
#endif /* filesys/directory.h */

