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

struct __attribute__((__packed__)) fileDescriptor {
    char* filename;
    int offset;
    int fd;
};

superblock_t superblock;
struct rootEntry *root;
struct fileDescriptor fileDescriptors[FS_OPEN_MAX_COUNT];
uint16_t *fat;
void* data;
int fatFree = 0;
int rootFree = 0;
int numOpen = 0;

int find_dataBlock(int offset, uint16_t firstBlock){
    return superblock->data + firstBlock + (offset / 4096);
}

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

    root = (rootEntry_t)malloc(FS_FILE_MAX_COUNT*sizeof(struct rootEntry));
    block_read(superblock->root, (void*)root);

    data = malloc(superblock->numDataBlocks*BLOCK_SIZE);
    for (int i = superblock->data; i < superblock->data + superblock->numDataBlocks; i++) {
        int increment = 0;
        block_read(i, (void*)(data + BLOCK_SIZE*increment));
        increment++;
    }

    fat[0] = FAT_EOC;
    for(int i = 0; i < superblock->numDataBlocks; i++) {
        if(fat[i] == 0) {
            fatFree++;
        }
    }

    for(int j = 0; j < FS_FILE_MAX_COUNT; j++) {
        if(root[j].filename[0] == 0) {
            rootFree++;
        }
    }

    for(int k = 0; k < FS_OPEN_MAX_COUNT; k++){
        fileDescriptors[k].filename = NULL;
        fileDescriptors[k].fd = -1;
        fileDescriptors[k].offset = -1;
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
    printf("FS Info:\n");
    printf("total_blk_count=%d\n", superblock->numBlocks);
    printf("fat_blk_count=%d\n", superblock->numFATBlocks);
    printf("rdir_blk=%d\n", superblock->root);
    printf("data_blk=%d\n", superblock->data);
    printf("data_blk_count=%d\n", superblock->numDataBlocks);
    printf("fat_free_ratio=%d/%d\n", fatFree, superblock->numDataBlocks);
    printf("rdir_free_ratio=%d/%d\n", rootFree, FS_FILE_MAX_COUNT);

for(int i = 0; i <128; i++) {
    printf("%s, %d\n", root[i].filename, root[i].firstBlock);
}
	return 0;
}

int fs_create(const char *filename)
{
    if (rootFree == 0 || strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0')
        return -1;

    /* don't create the file if a file with the same name already exists on the FS */
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename))
            return -1;
    }

    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(root[i].filename[0] == 0) {
            strcpy(root[i].filename, filename);
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            block_write(superblock->root, (void*) root);
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

    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].filename && !strcmp(fileDescriptors[k].filename, filename))
            return -1;
    }
 
   for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename)) {
            root[i].filename[0] = 0;
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            return 0;
        }
    }

       //TODO:clear associated data block ?

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
    int fileExists = 0;

    if (numOpen == FS_OPEN_MAX_COUNT) { //max open files currently open
       printf("max open files currently open\n");
       return -1;
    }
    if (strlen(filename) > FS_FILENAME_LEN) { // filename is invalid
       printf("filename too long\n"); 
       return -1;
    }
    if (filename[strlen(filename)] != '\0') { // filename is invalid
        printf("filename invalid\n");
        return -1;
    }

    for(int i = 0; i <= FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename)){ // file found
            fileExists = 1;
            break;
        }
    }

    if(fileExists == 0)
        return -1;

    for(int k = 0; k < FS_OPEN_MAX_COUNT; k++){
        if (fileDescriptors[k].fd == -1) {
            fileDescriptors[k].filename = (char*)malloc(FS_FILENAME_LEN);
            strcpy(fileDescriptors[k].filename, filename);
            fileDescriptors[k].fd = k;
            fileDescriptors[k].offset = 0;
            numOpen++;
            printf("fileDescriptor %d: %d\n", k, fileDescriptors[k].fd);
            return fileDescriptors[k].fd;
        }
    }
	return -1;
}

int fs_close(int fd)
{
    if (fd  >= FS_OPEN_MAX_COUNT || fd < 0) {
        return -1;
    }

    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) {
            free(fileDescriptors[k].filename);
            fileDescriptors[k].filename = NULL;       
            fileDescriptors[k].fd = -1;
            fileDescriptors[k].offset = -1;
            numOpen--;
            return 0;
        }
    }
    

	return -1; //file descriptor was not open
}

int fs_stat(int fd)
{
    if (fd >= FS_OPEN_MAX_COUNT || fd < 0) {
        return -1;
    }

    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) {
            for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
                if(!strcmp(root[i].filename, fileDescriptors[k].filename)) {
                    return root[i].size;
                }
            }   
        }
    }

	return -1;
}

int fs_lseek(int fd, size_t offset)
{
    if (fd  >= FS_OPEN_MAX_COUNT || fd < 0 || offset < 0) {
        return -1;
    }

    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) {
            for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
                if(!strcmp(root[i].filename, fileDescriptors[k].filename)) {
                    if(root[i].size < offset)
                        return -1;
                }
            }
            fileDescriptors[k].offset = offset;
            return 0;
        }
    }
 
	return -1; //file descriptor was not open
}

int fs_write(int fd, void *buf, size_t count)
{/*
    int fileOpen = 0;
    int offset, writeBlock;
    if (fd >= FS_OPEN_MAX_COUNT || fd < 0) {
        return -1;
    }

    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) { //check that file descriptor is open
            fileOpen = 1;
            for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
                if(!strcmp(root[i].filename, fileDescriptors[k].filename)) { //check that file exists on disk
                    offset = fileDescriptors[k].offset;
                    writeBlock = find_dataBlock(fileDescriptors[k].offset, root[i].firstBlock); // find the block to write to
                    break;
                }
            }
            break;
        }
    }

    if (!fileOpen)
        return -1;

    int remainingBytes = root[i].size - fileDescriptors[k].offset; //remainingBytes until end of file
    if (count > remainingBytes) //extend to hold additional bytes

*/
                        
    
	return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
    int fileOpen = 0;
    int readBlock, bytesToRead, blockBytes, offset, bytesRead = 0;
    void* bounceBuffer = NULL;
    if (fd >= FS_OPEN_MAX_COUNT || fd < 0) {
            return -1;
    }

    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) { //check that file descriptor is open
            fileOpen = 1;
            for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
                if(!strcmp(root[i].filename, fileDescriptors[k].filename)) { //check that file exists on disk
                    offset = fileDescriptors[k].offset;
                    readBlock = find_dataBlock(fileDescriptors[k].offset, root[i].firstBlock); // find the block to read from
                    int remainingBytes = root[i].size - fileDescriptors[k].offset; //remainingBytes until end of file
                    if (count > remainingBytes)
                        bytesToRead = remainingBytes;
                    else
                        bytesToRead = count;
                    break;
                }
            }
            break;
        }
    }

    if (!fileOpen)
        return -1;

    //TODO: test this case
    if (offset % BLOCK_SIZE != 0) { //first block to read is offset in the middle
        bounceBuffer = malloc(BLOCK_SIZE);
        block_read(readBlock, bounceBuffer);
        blockBytes = offset - BLOCK_SIZE*readBlock; //bytes to read from first block
        if (bytesToRead < blockBytes) {
            memcpy(buf, bounceBuffer, bytesToRead);
            bytesToRead -= bytesToRead;
            bytesRead += bytesToRead;
        } else {
            memcpy(buf, bounceBuffer, blockBytes);
            readBlock++;
            bytesRead += bytesToRead;
            bytesToRead -= blockBytes;
            buf += blockBytes;
        }
        free(bounceBuffer);
    }

    while (bytesToRead >= BLOCK_SIZE) { //TODO: test this case
        block_read(readBlock, buf);
        bytesToRead -= BLOCK_SIZE;
        bytesRead += BLOCK_SIZE;
        buf += BLOCK_SIZE;
        readBlock++;
    }

    if (bytesToRead != 0) {
        bounceBuffer = malloc(BLOCK_SIZE);
        block_read(readBlock, bounceBuffer);
        memcpy(buf, bounceBuffer, bytesToRead);
        bytesRead += bytesToRead;
        bytesToRead -= bytesToRead;
        free(bounceBuffer);
    }   
  
	return bytesRead;
}

