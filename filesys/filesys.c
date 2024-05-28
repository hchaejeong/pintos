#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	struct dir *dir;

	#ifdef FILESYS
	struct thread *current = thread_current();
	//struct dir *directory = current->current_dir;
	if (current->current_dir == NULL) {
		//NOT_REACHED();
		dir = dir_open_root();
	} else {
		dir = current->current_dir;
	}
	cluster_t clst = fat_create_chain(0);
	if (clst == 0) {
		fat_remove_chain(clst, 0);
		return false;
	}

	inode_sector = cluster_to_sector(clst);
	//create_file_inode(inode_open(inode_sector));
	bool success = (dir != NULL && inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	//dir = directory;
	#else
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	#endif
	if (!success && inode_sector != 0)
		//여기를 Fat_remove_chain으로 바꿔야할듯?
		#ifdef FILESYS
			fat_remove_chain(sector_to_cluster(inode_sector), 0);
		#else
			free_map_release (inode_sector, 1);
		#endif
	
	//dir_close (dir);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	struct thread *curr = thread_current();
	struct dir *dir;
	if (curr->current_dir == NULL) {
		dir = dir_open_root();
	} else {
		dir = curr->current_dir;
	}
	//struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct thread *curr = thread_current();
	struct dir *dir;
	if (curr->current_dir == NULL) {
		dir = dir_open_root();
	} else {
		dir = curr->current_dir;
	}
	//struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
