#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/fat.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	unsigned magic;                     /* Magic number. */
	uint32_t unused[124];               /* Not used. */
	bool directory;			//이 Inode가 파일인지 디렉토리인지
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
//컴퓨터가 인식하는 파일 구조체 느낌이라고 생각하면 된다
struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
};

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	//해당 inode를 갖고 있는 sector를 반환하는 함수이다
	//파일은 하나 이상의 섹터에 쪼개져서 저장될 것
	ASSERT (inode != NULL);
	/* 이전 방식을 파일이 연속적으로 할당되었다는 가정하에 함수가 작성되어있는데
	이제는 FAT를 이용해서 index 방식으로 여기저기 흩어져있는 섹터들에 파일을 저장하니까
	이거에 맞게 수정 필요.
	if (pos < inode->data.length)
		return inode->data.start + pos / DISK_SECTOR_SIZE;
	else
		return -1;
	*/
	if (pos < inode->data.length) {
		//현재 inode가 들어있는 섹터를 가져온다
		cluster_t pos_clst = sector_to_cluster(inode->data.start);
		cluster_t clst;
		//이제 해당 offset pos을 가진 위치로 가서 거기에 담겨있는 value를 찾고 섹터값으로 변환해줘야한다 
		for (int i = 0; i < (pos / DISK_SECTOR_SIZE); i++) {
			clst = fat_get(pos_clst);
			if (clst == 0) {
				return -1;
			}
			pos_clst = clst;
		}
		return cluster_to_sector(pos_clst);
	} else {
		return -1;
	}
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	//printf("size of disk_inode: %d", sizeof *disk_inode);
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);

	//disk inode를 만들고
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		
		#ifdef EFILESYS
		//새로운 chain을 만들어줘야하니까 0으로 입력해서 new chain이 allocate가 되는지 보고 (free한 공간 충분)
		cluster_t allocate;
		size_t count;
		if (sectors == 0) {
			disk_write(filesys_disk, sector, disk_inode);
			success = true;
		} else {
			//새로운 클러스터 체인을 일단 생성해줘야한다
			allocate = fat_create_chain(0);
			if (allocate == 0) {
				//fat안에 0인 연속적인 free한 공간이 있어야 하니까 0이 나오면 fail to allocate new cluster인거다
				//fat_remove_chain(allocate, 0);
				free(disk_inode);
				return false;
			}

			//FAT table안에서 빈 cluster들을 불러와서 파일 크기에 맞게 클러스터 체인을 만들어준다
			//이때 실제 값들을 넣어주는게 아닌 흩어진 섹터들을 연결해주는 작업만 한다!
			cluster_t new_clst;
			count = sectors;
			while (count > 0) {
				new_clst = fat_create_chain(allocate);
				if (new_clst == 0) {
					//chain에 cluster를 추가하는게 실패한거니까 free하고 revmoe해줘야한다
					fat_remove_chain(allocate, 0);
					free(disk_inode);
					return false;
				}

				allocate = new_clst;
				count--;
			}
			//여기까지 나온거면 chain이 다 성공적으로 만들어진거니까 이때 처음 만든 sector를 start로 지정해준다
			disk_inode->start = cluster_to_sector(allocate);
		}

		//이제 FAT 테이블에서는 다음 클러스터 번호가 잘 적혀있을거다
		//이제 또 하나하나씩 들어가서 disk_write을 호출해 클러스터에 해당 파일을 써줘야한다 
		disk_write(filesys_disk, sector, disk_inode);
		if (sectors > 0) {
			static char zeros[DISK_SECTOR_SIZE]; 
			cluster_t for_write = allocate;
			while (count > 0) {
				disk_write(filesys_disk, cluster_to_sector(for_write), zeros);
				//FAT안에 다음 cluster의 위치가 담겨있기때문에 이렇게 다음 cluster를 찾는다
				for_write = fat_get(for_write);

				count--;
			}
		}
		success = true;
		#else
		if (free_map_allocate (sectors, &disk_inode->start)) {
			disk_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++) 
					disk_write (filesys_disk, disk_inode->start + i, zeros); 
			}
			success = true; 
		} 
		
		free (disk_inode);
		#endif
	}
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
//파일이나 디렉토리를 열때 호출되는 이 함수를 통해서 inode구조체가 생성이 되어 메모리로 올라온다
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	/* 원래 코드
	disk_read (filesys_disk, inode->sector, &inode->data);
	*/
	disk_read(filesys_disk, cluster_to_sector(inode->sector), &inode->data);
	list_push_front (&open_inodes, &inode->elem);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	#ifdef EFILESYS
		disk_write (filesys_disk, inode->sector, &inode->data);
	#endif
	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			#ifdef EFILESYS
				fat_remove_chain(sector_to_cluster(inode->sector), 0);
			#else
				free_map_release (inode->sector, 1);
				free_map_release (inode->data.start,
						bytes_to_sectors (inode->data.length)); 
			#endif
		}

		free (inode); 
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			//printf("sector index num: %d", sector_idx);
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	disk_sector_t sector_idx = byte_to_sector (inode, offset + size);;
	if (sector_idx == -1) {
		//offset에 inode가 data를 가지고 있지 않은 경우이기 때문에 file을 늘려줘야한다
		int position = offset + size;
		//inode->data.length는 파일 사이즈 in bytes
		int start = (inode->data.length + DISK_SECTOR_SIZE - 1) / (DISK_SECTOR_SIZE * SECTORS_PER_CLUSTER);
		int end = (position + DISK_SECTOR_SIZE - 1) / (DISK_SECTOR_SIZE * SECTORS_PER_CLUSTER);
		
		cluster_t new_chain;
		if (inode->data.length == 0) {
			//file growth이 필요한거기 때문에 새로운 chain을 생성해줘야한다
			new_chain = fat_create_chain(0);
			if (new_chain == 0) {
				//NOT_REACHED();
				//chain이 만들어지지 않으면 file growth가 안되고 결국 실제로 데이터가 쓰여지지 않으니까 바로 0으로 반환시킨다
				return 0;
			} else {
				//NOT_REACHED();
				//inode에 정보를 업데이트 해줘야한다
				inode->data.start = cluster_to_sector(new_chain);
			}
		} else {
			//NOT_REACHED();
			//이미 데이터 섹션이 있는 경우이기 때문에 그냥 체인을 연결해준다
			new_chain = sector_to_cluster(inode->data.start);
			cluster_t val = fat_get(new_chain);
			while (val != EOChain) {
				new_chain = fat_get(new_chain);
			}
		}
		
		//NOT_REACHED();
		cluster_t chain = new_chain;
		for (int i = start; i < end; i++) {
			chain = fat_create_chain(chain);
			if (chain == 0) {
				// if (fat_get(new_chain) != EOChain)
				// 	fat_remove_chain(fat_get(new_chain), new_chain);
				return 0;
			}
		}
		
		//만약 여기까지 잘 나오면 chain이 우리가 원하는 만큼까지 커지는게 성공한거니까
		//그럼 inode의 데이터에 length를 우리가 쓰고 싶었던 크기를 넣어주면 된다
		inode->data.length = position;
		disk_write(filesys_disk, inode->sector, &inode->data);
	}

	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}

void
create_directory_inode (struct inode *inode) {
	inode->data.directory = true;
	disk_write(filesys_disk, inode->sector, &inode->data);
}

void
create_file_inode (struct inode *inode) {
	inode->data.directory = false;
	disk_write(filesys_disk, inode->sector, &inode->data);
}

bool
inode_is_directory (const struct inode *inode) {
	return inode->data.directory;
}