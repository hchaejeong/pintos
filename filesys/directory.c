#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir {
	struct inode *inode;                /* Backing store. */
	off_t pos;                          /* Current position. */
};

/* A single directory entry. */
struct dir_entry {
	disk_sector_t inode_sector;         /* Sector number of header. */
	char name[NAME_MAX + 1];            /* Null terminated file name. */
	bool in_use;                        /* In use or free? */
};

/* Creates a directory with space for ENTRY_CNT entries in the
 * given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	// printf("(dir_create)\n");
	bool created = inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
	if (created) {
		struct inode *opened = inode_open(sector);
		create_directory_inode(opened);
	}
	return created; // return값이 이게 안 추가되어있어서.. 
}

/* Opens and returns the directory for the given INODE, of which
 * it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		dir->inode = inode;
		dir->pos = 0;
		return dir;
	} else {
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* Opens the root directory and returns a directory for it.
 * Return true if successful, false on failure. */
struct dir *
dir_open_root (void) {
	#ifdef EFILESYS
		return dir_open(inode_open(cluster_to_sector(ROOT_DIR_SECTOR)));
	#else
		return dir_open (inode_open (ROOT_DIR_SECTOR));
	#endif
}

/* Opens and returns a new directory for the same inode as DIR.
 * Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) {
	if (dir != NULL) {
		inode_close (dir->inode);
		free (dir);
	}
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* Searches DIR for a file with the given NAME.
 * If successful, returns true, sets *EP to the directory entry
 * if EP is non-null, and sets *OFSP to the byte offset of the
 * directory entry if OFSP is non-null.
 * otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

/* Searches DIR for a file with the given NAME
 * and returns true if one exists, false otherwise.
 * On success, sets *INODE to an inode for the file, otherwise to
 * a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (lookup (dir, name, &e, NULL))
		*inode = inode_open (e.inode_sector);
	else
		*inode = NULL;

	return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
 * file by that name.  The file's inode is in sector
 * INODE_SECTOR.
 * Returns true if successful, false on failure.
 * Fails if NAME is invalid (i.e. too long) or a disk or memory
 * error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) {
	struct dir_entry e;
	off_t ofs;
	bool success = false;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* Check NAME for validity. */
	if (*name == '\0' || strlen (name) > NAME_MAX)
		return false;

	/* Check that NAME is not in use. */
	if (lookup (dir, name, NULL, NULL))
		goto done;

	/* Set OFS to offset of free slot.
	 * If there are no free slots, then it will be set to the
	 * current end-of-file.

	 * inode_read_at() will only return a short read at end of file.
	 * Otherwise, we'd need to verify that we didn't get a short
	 * read due to something intermittent such as low memory. */
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (!e.in_use)
			break;

	/* Write slot. */
	e.in_use = true;
	strlcpy (e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
	return success;
}

/* Removes any entry for NAME in DIR.
 * Returns true if successful, false on failure,
 * which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) {
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	//printf("(dir_remove) 들어가?\n");

	/* Find directory entry. */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

	/* Open inode. */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

	// 만약에 dir인 경우,
	bool is_dir = inode_is_directory(inode);
	if (is_dir) {
		//일단 이 directory 아래의 파일이나 subdirectory가 존재하면 안된다
		struct dir *curr_dir = dir_open(inode);

		//이 directory안에 있는 엔트리들을 찾아보면서 열려있는게 있는지 체크해야한다
		struct dir_entry temp;
		size_t dir_entry_size = sizeof(temp);
		
		//off_t curr_dir_pos = curr_dir->pos;
		//NOT_REACHED();
		while (true) {
			bool not_all_entry = inode_read_at(inode, &temp, dir_entry_size, curr_dir->pos) < dir_entry_size;
			//이 directory의 마지막 directory entry까지 검사하는거기 때문에 엔트리 하나 보다 작은 사이즈만 읽을 수 있으면
			//마지막 엔트리에 도달한거다
			if (not_all_entry) {
				//NOT_REACHED();
				break;
			}
			
			//이제 temp에 현재 찾은 directory의 dir entry를 하나하나씩 읽고 넣어준다
			//만약 지금 directory안에 entry가 사용되고 있는거면 . 또는 .. 아니면 안된다
			curr_dir->pos += dir_entry_size;
			if (temp.in_use) {
				//printf("temp name: %s", temp.name);
				//"." 또는 ".." 이외에 파일 또는 디렉토리가 있으면 안된다
				int cur_dir = strcmp(temp.name, ".");
				int parent_dir = strcmp(temp.name, "..");
				if (cur_dir != 0 && parent_dir != 0) {
					//printf("compare with . is %d", cur_dir);
					//printf("compare with .. is %d", parent_dir);
					//NOT_REACHED();
					//"." 또는 ".."이 둘 다 아닌데 사용되고 있는게 있는 상황이니까 제거하면 안된다
					dir_close(curr_dir);
					return false;
				}
			}
		}

		//지금 현재 프로세스에서 사용되고 있는 디렉토리면 안된다
		struct dir *process_dir = thread_current()->current_dir;
		if (process_dir != NULL) {
			//NOT_REACHED();
			struct inode *process_inode = dir_get_inode(process_dir);
			if (process_inode == inode) {
				//현재 사용되고 있는 inode가 같은 디렉토리의 Inode일때는 이 디렉토리를 제거하면 안된다
				dir_close(curr_dir);
				return false;
			}
		}
		
		//만약 이 디렉토리가 두군데 이상 사용되고 있으면 이 디렉토리는 제거할 수 없다
		if (get_open_count(inode) > 2) {
			//NOT_REACHED();
			dir_close(curr_dir);
			return false;
		}
	}

	/* Erase directory entry. */
	e.in_use = false;
	if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

	/* Remove inode. */
	inode_remove (inode);
	success = true;

done:
	inode_close (inode);
	return success;
}

/* Reads the next directory entry in DIR and stores the name in
 * NAME.  Returns true if successful, false if the directory
 * contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		dir->pos += sizeof e;
		// dir 경로 안에 .나 .. 있으면 그건 pass 해야함
		// strcmp의 반환 값이 0이면 같다는 것임
		if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) {
			continue;
		}
		if (e.in_use) {
			strlcpy (name, e.name, NAME_MAX + 1);
			return true;
		}
	}
	return false;
}

bool dir_pos (struct dir *dir) {
	return dir->pos;
}

void dir_change_pos (struct dir *dir) {
	dir->pos = 2*sizeof(struct dir_entry);
}