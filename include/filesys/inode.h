#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t, bool);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
// project 4
void create_directory_inode (struct inode *inode);
void create_file_inode (struct inode *inode);
bool inode_is_directory (const struct inode *inode);
bool create_link_inode (disk_sector_t sector, char *target);
bool check_symlink(struct inode *inode);
char copy_inode_link (struct inode *inode, char *path);
char inode_data_symlink_path (struct inode *inode);
int length_symlink_path (struct inode *inode);

// bool is_file_dir (struct file *file);

#endif /* filesys/inode.h */
