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

	thread_current()->current_dir = dir_open_root(); // root 정보를 기본적으로 깔고 감
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

	#ifdef EFILESYS

	char *copy_name = (char*)malloc(strlen(name) + 1);
	strlcpy(copy_name, name, strlen(name) + 1);

	char *final_name = (char*)malloc(strlen(name) + 1);

	dir = dir_open_root();
	if (copy_name[0] != '/') {
		dir = dir_reopen(thread_current()->current_dir);
	}
	dir = parsing(dir, copy_name, final_name);

	// 여기서부터는 채정이가 쓴 것
	cluster_t clst = fat_create_chain(0);
	/*
	if (clst == 0) {
		fat_remove_chain(clst, 0);
		return false;
	}
	*/

	//inode_sector = cluster_to_sector(clst);
	//create_file_inode(inode_open(inode_sector));
	bool success = (dir != NULL && inode_create (clst, initial_size, false)
			&& dir_add (dir, name, clst));
	//dir = directory;
	// 이건 원래 적힌 filesys_create 함수처럼 해주면 됨
	if (!success && clst != 0) {
		fat_remove_chain(clst, 0);
	}

	free(copy_name);
	free(final_name);
	dir_close(dir);

	return success;

	#else
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (dir, name, inode_sector));
	#endif
	if (!success && inode_sector != 0)
		//여기를 Fat_remove_chain으로 바꿔야할듯?
		#ifdef EFILESYS
			fat_remove_chain(sector_to_cluster(inode_sector), 0);
		#else
			free_map_release (inode_sector, 1);
		#endif
	
	//dir_close (dir);
	create_file_inode(inode_open(inode_sector));

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	#ifdef EFILESYS
		
		char *copy_name = (char*)malloc(strlen(name) + 1);
		strlcpy(copy_name, name, strlen(name) + 1);

		char *final_name = (char*)malloc(strlen(name) + 1);

		struct inode *inode = NULL;

		// 여기는 chdir과 같음. 경로 초기 세팅
		struct dir *dir = dir_open_root(); // 일단 기본이 되는 root 경로로 열어두고 시작
		if (copy_name[0] != '/') {
			dir = dir_reopen(thread_current()->current_dir);
		}
		dir = parsing(dir, copy_name, final_name);
		if (dir != NULL) {
			dir_lookup(dir, final_name, &inode);
			if ((inode != NULL) && check_symlink(inode)) {
				dir_close(dir);
				// 만약에 symlink file이면, name을 symlink_path로 바꿔줘야 함
				char *link_name = (char*)malloc(length_symlink_path(inode));
				strlcpy(link_name, inode_data_symlink_path(inode), length_symlink_path(inode));
				dir = dir_open_root();
				if (link_name[0] != '/') {
					dir = dir_reopen(thread_current()->current_dir);
				}
				char *link_final_name = (char*)malloc(length_symlink_path(inode));
				dir = parsing(dir, link_name, link_final_name);
				if (dir != NULL) {
					dir_lookup(dir, link_final_name, &inode);
				}
				free(link_name);
				free(link_final_name);
			}
		}
		free(copy_name);
		free(final_name);
		dir_close(dir);
		
		return file_open(inode);

	#else
		struct thread *curr = thread_current();
		struct dir *dir;
		/*
		if (curr->current_dir == NULL) {
			dir = dir_open_root();
		} else {
			dir = curr->current_dir;
		}
		*/
		struct dir *dir = dir_open_root ();
		struct inode *inode = NULL;

		if (dir != NULL)
			dir_lookup (dir, name, &inode);
		dir_close (dir);

		return file_open (inode);
	#endif
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	#ifdef EFILESYS
		char *copy_name = (char*)malloc(strlen(name) + 1);
		strlcpy(copy_name, name, strlen(name) + 1);

		char *final_name = (char*)malloc(strlen(name) + 1);

		struct inode *inode = NULL;

		bool ret = false;

		// 여기는 chdir과 같음. 경로 초기 세팅
		struct dir *dir = dir_open_root(); // 일단 기본이 되는 root 경로로 열어두고 시작
		if (copy_name[0] != '/') {
			dir = dir_reopen(thread_current()->current_dir);
		}
		dir = parsing(dir, copy_name, final_name);

		if (dir != NULL) {
			dir_lookup(dir, final_name, &inode);
			
			//filesys_remove에서는, dir인 경우와 file인 경우로 나눠서 처리해줘야 함
			if (inode_is_directory(inode)) {
				// dir인 경우!
				struct dir *dir_now = dir_open(inode);
				dir_change_pos(dir_now);

				char *empty_name = (char *)malloc(NAME_MAX + 1);
				if (dir_readdir(dir_now, empty_name)) {
					// dir이 비지 않은 경우 (name이 찼다!)
					// 그러면 파일을 지우면 됨!
					ret = dir_remove(dir_now, final_name);
				} else {
					// dir이 빈 경우
					// 지금 이 dir에 current thread가 접속해있으면 지우면 안됨.
					// 그게 아니라면 지워야함 ㅇㅇ
					struct inode *current_inode = dir_get_inode(thread_current()->current_dir);
					struct inode *dir_now_inode = dir_get_inode(dir_now);
					if (inode_get_inumber(current_inode) != inode_get_inumber(dir_now_inode)) {
						ret = dir_remove(dir, final_name);
					}
				}
				dir_close(dir_now);
				free(empty_name);
			} else {
				// file인 경우! 파일만 잘 닫으면 됨~
				inode_close(inode);
				ret = dir_remove(dir, final_name);
			}
		}

		free(copy_name);
		free(final_name);
		dir_close(dir);

		return ret;

	#else
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
	#endif
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

// syscall.c의 mkdir이랑 symlink에서 썼던 parsing하는게 계속 쓰이네 여기서도
struct dir *parsing(struct dir *dir, char *path, char *final_name) {
	if (path == NULL || strlen(path) == 0) {
		return NULL;
	}

	char *save;
	char *path_token = strtok_r(path, "/", &save);
	struct inode *inode = NULL;

	if (path_token == NULL) {
		strlcpy(final_name, ".", 2); // "/"인 경우에는, file_name은 .이 되어야 함
	}
	
	// 여기서의 목적은 file_name만 parsing 해내는 것!
	while (path_token != NULL) {
		bool success_lookup = dir_lookup(dir, path_token, &inode);
		bool is_inode_dir = inode_is_directory(inode);
		if (!(success_lookup && is_inode_dir)) {
			dir_close(dir);
			inode_close(inode);
			dir = NULL; // dir이 없는 거니까 dir은 null로 세팅해줘야
			break;
		} else {
			// 링크 파일인 경우는 앞서서 한 번 더 돌리면 될 것 같음
			if (check_symlink(inode)) {
				// 아니 대체 왜 inode->data.symlink가 안되는건데 ㅋㅋ
				// inode의 path를 복사해온다!
				char *inode_path = (char*)malloc(sizeof(length_symlink_path(inode)));
				strlcpy(inode_path, inode_data_symlink_path(inode), length_symlink_path(inode));
				
				// inode path 경로에 symlink 뒷부분을 갖다붙이면 됨
				// ../file 이런 식으로 되어있던 걸 inode path/file 이런 형식으로!
				strlcat(inode_path, "/", strlen(inode_path) + 2);
				strlcat(inode_path, save, strlen(inode_path) + strlen(save) + 1);

				dir_close(dir);

				// 그리고 while문 다시 시작해야함!
				dir = dir_open_root();
				if (path[0] != '/') {
					dir = dir_reopen(thread_current()->current_dir);
				}
				strlcpy(path, inode_path, strlen(inode_path) + 1);
				free(inode_path);
				path_token = strtok_r(path, "/", &save);
				continue;
			}


			dir_close(dir);
			dir = dir_open(inode);
			// 근데 여기서, 경로의 마지막은 file의 이름이므로 그걸 저장해야함
			char *check_next = strtok_r(NULL, "/", &save);
			if (check_next == NULL) {
				// file_name에 file name 복사해두기!
				strlcpy(final_name, path_token, strlen(path_token) + 1);
				break;
			} else {
				path_token = final_name;
			}
		}
	}

	return dir;
}
