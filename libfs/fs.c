#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

typedef struct rootEntry* rootEntry_t;
typedef struct superblock* superblock_t;
 
struct __attribute__((__packed__)) superblock {
    char signature[8];
    uint16_t numBlocks;
    uint16_t root;
    uint16_t data;
    uint16_t numDataBlocks;
    uint8_t numFATBlocks;
    char padding[4079];
};

struct __attribute__((__packed__)) rootEntry {
    char filename[16];
    uint32_t size;
    uint16_t firstBlock;
    char padding[10];
};

superblock_t superblock;
struct rootEntry *root;
uint16_t *fat;
int fatFree = 0;
int rootFree = 0;
int numOpen = 0;

int fs_mount(const char *diskname)
{
    if(block_disk_open(diskname)) {
        return -1;
    }

    superblock = (superblock_t)malloc(sizeof(struct superblock));
    block_read(0, (void*)superblock);
    if (memcmp(superblock->signature, "ECS150FS", 8)) {
       return -1;
    }

    fat = (uint16_t*)malloc(superblock->numDataBlocks*sizeof(uint16_t));
    for (int i = 1; i <= superblock->numFATBlocks; i++) {
        block_read(i, (void*)(fat + BLOCK_SIZE*(i - 1)));
    }
/*    if (fat[0] != FAT_EOC) {
        return -1;
    }
*/
    root = (rootEntry_t)malloc(FS_FILE_MAX_COUNT*sizeof(struct rootEntry));
    block_read(superblock->root, (void*)root);

    for(int i = 1; i < superblock->numDataBlocks; i++) {
        if(fat[i] == 0) {
            fatFree++;
        }
    }

    for(int j = 0; j < FS_FILE_MAX_COUNT; j++) {
        if(root[j].filename[0] == 0) {
            rootFree++;
        }
    }

	return 0;
}

int fs_umount(void)
{
//    free(superblock);
//    free(fat);
    
    block_write(superblock->root, (void*)root);
    if (block_disk_close()) {
        return -1;
    }

	return 0;
}

int fs_info(void)
{
    printf("FS Info\n");
    printf("total_blk_count=%d\n", superblock->numBlocks);
    printf("fat_blk_count=%d\n", superblock->numFATBlocks);
    printf("rdir_blk=%d\n", superblock->root);
    printf("data_blk=%d\n", superblock->data);
    printf("data_blk_count=%d\n", superblock->numDataBlocks);
    printf("fat_free_ratio=%d/%d\n", fatFree, superblock->numDataBlocks);
    printf("rdir_free_ratio=%d/%d\n", rootFree, FS_FILE_MAX_COUNT);
	return 0;
}

int fs_create(const char *filename)
{
    if (rootFree == 0)
        return -1;
    if (strlen(filename) > FS_FILENAME_LEN)
        return -1;
    if (filename[strlen(filename)] != '\0')
        return -1;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename))
            return -1;
    }
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(root[i].filename[0] == 0) {
            strcpy(root[i].filename, filename);
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            return 0;
        }
    }
 
	return -1;
}

int fs_delete(const char *filename)
{
    if (strlen(filename) > FS_FILENAME_LEN)
        return -1;
    if (filename[strlen(filename)] != '\0')
        return -1;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename)) {
            root[i].filename[0] = 0;
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            return 0;
        }
    }

    //return -1 if file is open...
	return -1;
}

int fs_ls(void)
{
    printf("FS Ls:\n");
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(root[i].filename[0] != 0) {
            printf("file: %s, size: %d, data_blk: %d\n", root[i].filename, root[i].size, root[i].firstBlock);
        }
    } 
	return 0;
}

int fs_open(const char *filename)
{
    if (numOpen == FS_OPEN_MAX_COUNT)
        return -1; 
    if (strlen(filename) > FS_FILENAME_LEN)
        return -1;
    if (filename[strlen(filename)] != '\0')
        return -1;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename))
            return -1;
    }
    int fd = open(filename, O_RDWR);
    numOpen++;
	return fd;
}

int fs_close(int fd)
{
    if (fd > 31 || fd < 0) {
        return -1;
    }
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

