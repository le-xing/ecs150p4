#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

typedef struct rootEntry* rootEntry_t;
typedef struct superblock* superblock_t;
typedef struct FAT* fat_t;
typedef struct rootDirectory* rootDirectory_t;
 
struct __attribute__((__packed__)) superblock {
    char signature[8];
    uint16_t numBlocks;
    uint16_t root;
    uint16_t data;
    uint16_t numDataBlocks;
    uint8_t numFATBlocks;
    char padding[4079];
};

struct __attribute__((__packed__)) FAT {
    uint16_t *entries;
};

struct __attribute__((__packed__)) rootEntry {
    char filename[16];
    uint32_t size;
    uint16_t firstBlock;
    char padding[10];
};

struct __attribute__((__packed__)) rootDirectory {
    struct rootEntry entries[128];
};


superblock_t superblock;
fat_t fat;
rootDirectory_t root;


int fs_mount(const char *diskname)
{
    if(block_disk_open(diskname)) {
        return -1;
    }

    block_read(0, (void*)superblock);
    if (memcmp(superblock->signature, "ECS150FS", 8)) {
       return -1;
    }

    fat->entries = (uint16_t*)malloc(superblock->numFATBlocks*BLOCK_SIZE);
    for (int i = 1; i <= superblock->numFATBlocks; i++) {
        block_read(i, (void*)fat);
    }

    if (fat->entries[0] != FAT_EOC) {
        return -1;
    }

    block_read(superblock->root, (void*)root);

	return 0;
}

int fs_umount(void)
{
    
    block_disk_close();
	return 0;
}

int fs_info(void)
{
	return 0;
}

int fs_create(const char *filename)
{
	return 0;
}

int fs_delete(const char *filename)
{
	return 0;
}

int fs_ls(void)
{
	return 0;
}

int fs_open(const char *filename)
{
	return 0;
}

int fs_close(int fd)
{
	return 0;
}

int fs_stat(int fd)
{
	return 0;
}

int fs_lseek(int fd, size_t offset)
{
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
	return 0;
}

