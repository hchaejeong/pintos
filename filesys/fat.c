#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs; //부팅 시 FAT 정보를 담는 구조체
	unsigned int *fat;	//FAT
	unsigned int fat_length;	//file system안에 들어가있는 섹터의 수
	disk_sector_t data_start;	//비어있는 첫 섹터
	cluster_t last_clst;	//file이 할당받은 cluster 중, 마지막 cluster
	struct lock write_lock;	
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	//fat_fs의 fat_length랑 data_start 필드를 초기화시켜준다
	//fat_length는 파일 시스템에 있는 총 cluster의 개수를 담고 있어야한다 (파일 시스템 자체가 FAT이다)
	//cluster는 여러 sector들로 이루어져있다
	struct fat_boot booting_info = fat_fs->bs;
	fat_fs->fat_length = booting_info.total_sectors - booting_info.fat_sectors - 1 / SECTORS_PER_CLUSTER;
	//data_start은 어떤 sector에서부터 파일을 저장할 수 있는지를 알려준다
	//실제 데이터 부분들은 fat table의 entries 뒤에 오기 때문에 fat가 시작한 지점에서 fat의 총 크기를 더하면 data의 시작점을 구할 수 있다
	fat_fs->data_start = booting_info.fat_start + booting_info.fat_sectors;
	//last_clst랑 write_lock은 다른 곳에서 init안되고 있으니까 여기서 해줘야한다
	fat_fs->last_clst = booting_info.total_sectors + 1;
	lock_init(&fat_fs->write_lock);
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

cluster_t
get_free_cluster() {
	cluster_t free_space = 0;
	for (cluster_t entry = fat_fs->bs.root_dir_cluster + 1; entry < (cluster_t)fat_fs->fat_length; entry++) {
		if (fat_get(entry) == 0) {
			//fat가 값이 0이면 free하다는 뜻이니까 이 처음 위치를 받아서 이걸 clst의 값으로 해서 연결시킨다
			free_space = entry;
			break;
		}
	}

	return free_space;
}

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	//int *fat = fat_fs->fat;
	cluster_t free_space = get_free_cluster();

	if (clst == 0) {
		//여기서 새로운 chain을 만드니까 지금 들어온 clst가 chain의 첫 클러스터가 된다
		//새로운 Chain을 만들기 위해 free한 공간을 하나 찾아서 배정해줘야한다
		//아직 이거만 있으니까 end of file일테니 EOChain으로 표시해준다
		if (free_space != 0) {
			fat_put(free_space, EOChain);
		} else {
			return 0;
		}
	} else {
		//해당 clst에 새로운 cluster를 하나 추가해주는거기 때문에 
		//할당할 수 있는 free cluster를 찾고 이 위치넘버를 현재 clst의 값으로 넣어서 연결해줘야한다
		if (free_space != 0) {
			fat_put(clst, free_space);
		} else {
			return 0;
		}

		//여기서도 이 새롭게 추가해줄 cluster가 결국에 이 chain의 마지막 부분이 되는거니까 EOChain으로 세팅해야한다
		fat_put(free_space, EOChain);
	}
	char zero_buf[DISK_SECTOR_SIZE] = {0};
	disk_write(filesys_disk, cluster_to_sector(free_space), zero_buf);

	return free_space;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	//clst에서 시작해서 이어지는 cluster들을 제거해야하니까 여기서 EOChain을 가진 cluster를 찾을때까지
	//각 fat entry에 0으로 free하다고 값을 바꿔줘야한다
	//cluster_t entry = fat_get(clst);	
	while (fat_get(clst) != EOChain) {
		cluster_t entry = fat_get(clst);
		//cluster_t curr_val = fat_get(clst);
		fat_put(entry, 0);
		clst = entry;
	}

	if (pclst == 0) {
		//그럼 clst가 beginning of chain이기 때문에 이거를 0으로 바꿔줘야한다
		fat_put(clst, 0);
	} else {
		//0이 아닌경우에는 우리가 제거한 chain of cluster의 바로 직전 entry를 가지고 있으니
		//지금 제거한거랑 구분하기 위해서 이 pclst는 end of chain이라는걸 표시해줘야한다
		fat_put(pclst, EOChain);
	}
}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	//clst가 포인트하고 있는 FAT entry에 val로 업데이트해준다
	//결국 이 clst번째에 있는 FAT가 다른 cluster랑 연결되도록 point하는 index를 바꿔주는거다
	int *fat = fat_fs->fat;
	fat[clst] = val;
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	//clst가 어떤 cluster를 point하는지를 찾는거기 때문에 FAT에서 clst에 들어있는 값만 빼오면 된다
	int *fat = fat_fs->fat;
	return fat[clst];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	// printf("(cluster_to_sector) clst: %d, fat_fs->fat_length: %d\n", clst, fat_fs->fat_length);
	// printf("(cluster_to_sector) if clst is 0?: %s\n", clst == 0? "true":"false");
	ASSERT(clst > 0 && clst < fat_fs->fat_length);
	return fat_fs->data_start + (clst - 1) * SECTORS_PER_CLUSTER;
}

//inode.c에서 쓰이는 conversion 함수
cluster_t
sector_to_cluster (disk_sector_t sector) {
	// printf("(sector_to_cluster) sector: %d, fat_fs->data_start: %d\n", sector, fat_fs->data_start);
	disk_sector_t difference = sector - fat_fs->data_start;
	return (cluster_t) difference + 1;
}
