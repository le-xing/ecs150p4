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
struct superblock sblock;
struct rootEntry root[128];
struct fileDescriptor fileDescriptors[FS_OPEN_MAX_COUNT];
uint16_t *fat = NULL;
void* data;
int fatFree = 0;
int rootFree = 0;
int numOpen = 0;

superblock_t initSuperblock() {
    memset(sblock.signature, 0, 8);
    sblock.numBlocks = sblock.root = sblock.data = sblock.numDataBlocks = sblock.numFATBlocks = 0;
    memset(sblock.padding, 0, 4079);
    return &sblock;
}

int find_remainingBytes(int fd, int i) //find remainingBytes from offset until end of file of a file identified by a given fd
{
	return root[i].size - fileDescriptors[fd].offset;
}

int find_dataBlock(int fd, int *bytesToRead)
{
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    	if(!strcmp(root[i].filename, fileDescriptors[fd].filename)) { // Find the associated root entry to get file size
			int remainingBytes = find_remainingBytes(fd, i); // Bytes that can be read 
			if (*bytesToRead > remainingBytes)
				*bytesToRead = remainingBytes;
			return superblock->data + root[i].firstBlock + (fileDescriptors[fd].offset / BLOCK_SIZE); // First block to read from
        }
    }
    return 0;
}

int fs_mount(const char *diskname)
{
    // Open the disk
    if(block_disk_open(diskname)) {
        return -1;
    }

    // Read the superblock
    superblock = initSuperblock();
    block_read(0, (void*)superblock);
    if (memcmp(superblock->signature, "ECS150FS", 8)) {
       return -1;
    }
    // Read FAT blocks
    fat = (uint16_t*)malloc(superblock->numDataBlocks*sizeof(uint16_t));
printf("data: %d, fat: %d ", superblock->numDataBlocks, superblock->numFATBlocks);
    for (int i = 1; i < superblock->root; i++) {
        block_read(i, (void*)(fat + BLOCK_SIZE*(i - 1)));
    }

printf("root: %d", superblock->root);
    // Read the root block
    block_read(superblock->root, (void*)root);

//TODO: is this even needed?
    // Read data blocks
    data = malloc(superblock->numDataBlocks*BLOCK_SIZE);
    for (int i = superblock->data; i < superblock->data + superblock->numDataBlocks; i++) {
        int increment = 0;
        block_read(i, (void*)(data + BLOCK_SIZE*increment));
        increment++;
    }

    fat[0] = FAT_EOC; //TODO: do we need to manually make first FAT entry FAT_EOC??

    // Count available entries in FAT
/*    for (int i = 0; i < superblock->numDataBlocks; i++) {
        if(fat[i] == 0) {
            fatFree++;
        }
    }
*/
    // Count available entries in root
    for (int j = 0; j < FS_FILE_MAX_COUNT; j++) {
        if(root[j].filename[0] == 0) {
            rootFree++;
        }
    }

    // Initialize file descriptors
    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        fileDescriptors[k].filename = (char*)malloc(FS_FILENAME_LEN);
        fileDescriptors[k].fd = -1;
        fileDescriptors[k].offset = -1;
    }

	return 0;
}

int fs_umount(void)
{
printf("unmounting\n");
    //TODO: write to all blocks, or just modified ones? inside umount() or inside functions that will modify blocks?
    block_write(superblock->root, (void*)root);

    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        free(fileDescriptors[k].filename);
    }

    free(data);
    //free(fat); // segfault, TODO: will fix later, has to do with block_read during mount, refer to piazza @879
    
    // Close the disk
    if (block_disk_close()) {
printf("closing disk\n");
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
	return 0;
}

int fs_create(const char *filename)
{
    if (rootFree == 0 || strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0')
        return -1;

    // Don't create file if a file with the same name already exists on the FS
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename))
            return -1;
    }

    // Create empty file and add root entry
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(root[i].filename[0] == 0) { // find the first empty root entry
            strcpy(root[i].filename, filename);
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            block_write(superblock->root, (void*) root);
            return 0;
        }
    }
 
    // Function should not reach here if file successfully created
	return -1; 
}

//TODO: Test delete on non empty file, check info after (fatFree specifically)
int fs_delete(const char *filename)
{
    if (strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0')
        return -1;

    // Don't delete file if it is open
    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].filename && !strcmp(fileDescriptors[k].filename, filename))
            return -1;
    }
 
    // TODO: reset FAT entries and clear associated data blocks
    // don't need to free the data block, just clear it somehow (use memset)
    // remember to write to disk (blocks were changed)

   // Reset the associated root entry
   for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename)) {
            root[i].filename[0] = 0;
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            return 0;
        }
    }

    // Function should not reach here
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

    if (numOpen == FS_OPEN_MAX_COUNT || strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0') 
       return -1;
 
    // Check if the file exists on the disk 
    for(int i = 0; i <= FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename)){ // file found
            fileExists = 1;
            break;
        }
    }

    if(fileExists == 0)
        return -1;

    // Open the file and assign a file descriptor entry
    for(int k = 0; k < FS_OPEN_MAX_COUNT; k++){
        if (fileDescriptors[k].fd == -1) {
            strcpy(fileDescriptors[k].filename, filename);
            fileDescriptors[k].fd = k;
            fileDescriptors[k].offset = 0;
            numOpen++;
            return fileDescriptors[k].fd;
        }
    }

    // Function should not reach here if file successfully opened
	return -1;
}

int fs_close(int fd)
{
    // Check if @fd valid and file with @fd is open
    if (fd  >= FS_OPEN_MAX_COUNT || fd < 0 || fileDescriptors[fd].fd != fd) {
        return -1;
    }

//NOTE: since fd = k assigned in open, probably don't have to use a for loop to find it

    // Reset associated fileDescriptors entry
    memset(fileDescriptors[fd].filename, 0, FS_FILENAME_LEN);
    fileDescriptors[fd].fd = -1;
    fileDescriptors[fd].offset = -1;
    return 0;
/*
    // Reset associated fileDescriptors entry
    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) {
            memset(fileDescriptors[k].filename, 0, FS_FILENAME_LEN); 
            fileDescriptors[k].fd = -1;
            fileDescriptors[k].offset = -1;
            numOpen--;
            return 0;
        }
    }
    
    // File descriptor @fd was not open
	return -1;
*/
}

int fs_stat(int fd)
{
    // Check if @fd valid and file with @fd is open
    if (fd >= FS_OPEN_MAX_COUNT || fd < 0 || fileDescriptors[fd].fd != fd) {
        return -1;
    }

    // Find associated root entry to get file size
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (!strcmp(root[i].filename, fileDescriptors[fd].filename)) {
            return root[i].size;
        }
    }

/*
    // Find the file size of the file associated with file descriptor @fd
    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) {
            for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
                if(!strcmp(root[i].filename, fileDescriptors[k].filename)) {
                    return root[i].size;
                }
            }   
        }
    }
*/
    // Function should not reach here if file size successfully found
	return -1;
}

int fs_lseek(int fd, size_t offset)
{
    // Check if @fd and @offset valid and file with @fd is open
    if (fd  >= FS_OPEN_MAX_COUNT || fd < 0 || offset < 0 || offset > fs_stat(fd) || fileDescriptors[fd].fd != fd) {
        return -1;
    }

    fileDescriptors[fd].offset = offset;
    return 0;
/*

    // Find the associated fileDescriptors entry 
    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].fd == fd) {
            fileDescriptors[k].offset = offset;
            return 0;
        }
    }
 
	return -1; //file descriptor was not open
*/
}

int fs_write(int fd, void *buf, size_t count)
{
          
    
	return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
    //int fileOpen = 0;
    int* bytesToRead = (int *)malloc(sizeof(count));
    int readBlock, blockBytes, bytesRead = 0;
    void* bounceBuffer = NULL;
    if (fd >= FS_OPEN_MAX_COUNT || fd < 0 || fileDescriptors[fd].fd != fd) {
            return -1;
    }

    *bytesToRead = count;
    readBlock = find_dataBlock(fd, bytesToRead);
    
    //TODO: test this case
    if (fileDescriptors[fd].offset % BLOCK_SIZE != 0) { // Offset is in middle of a block
        bounceBuffer = malloc(BLOCK_SIZE);
        block_read(readBlock, bounceBuffer);
        blockBytes = fileDescriptors[fd].offset - BLOCK_SIZE*readBlock; // Bytes to read from first block
        if (*bytesToRead < blockBytes) { // Only need to read from this one block
            memcpy(buf, bounceBuffer, *bytesToRead);
            *bytesToRead -= *bytesToRead; // bytesToRead = 0
            bytesRead += *bytesToRead;
        }
        else { // Read to the end of the block
            memcpy(buf, bounceBuffer, blockBytes);
            readBlock++;
            bytesRead += blockBytes;
            *bytesToRead -= blockBytes;
            buf += blockBytes; //TODO: maybe don't modify buf directly; use a temp pointer initialized to buf instead
            memset(bounceBuffer, 0 , BLOCK_SIZE); // Clear the bounce buffer for possible later use
        }
    }

    // Read whole blocks until no longer possible
    while (*bytesToRead >= BLOCK_SIZE) { //TODO: test this case
        block_read(readBlock, buf);
        *bytesToRead -= BLOCK_SIZE;
        bytesRead += BLOCK_SIZE;
        buf += BLOCK_SIZE; //TODO: change buf to temp?
        readBlock++;
    }

    // Read any remaining bytes
    if (*bytesToRead != 0) {
        if (!bounceBuffer)
            bounceBuffer = malloc(BLOCK_SIZE);
        block_read(readBlock, bounceBuffer);
        memcpy(buf, bounceBuffer, *bytesToRead);
        bytesRead += *bytesToRead;
        *bytesToRead -= *bytesToRead;
    }   

    if (bounceBuffer)
        free(bounceBuffer);
  
    fileDescriptors[fd].offset += bytesRead; 
	return bytesRead;
}

