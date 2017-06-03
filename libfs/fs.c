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

typedef enum {
    READ,
    WRITE,
} rwFlag;

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
int fatFree = 0;
int rootFree = 0;
int numOpen = 0;

superblock_t initSuperblock() {
    memset(sblock.signature, 0, 8);
    sblock.numBlocks = sblock.root = sblock.data = sblock.numDataBlocks = sblock.numFATBlocks = 0;
    memset(sblock.padding, 0, 4079);
    return &sblock;
}

// Find index of datablock corresponding to offset and find # of bytes to modify
int find_dataBlock(int fd, int *bytesToModify, rwFlag rw)
{ 
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    	if(!strcmp(root[i].filename, fileDescriptors[fd].filename)) { // Find the associated root entry to get file size
			int remainingBytes = root[i].size - fileDescriptors[fd].offset; // Bytes until end of file
			if(rw == READ) {
                if (*bytesToModify > remainingBytes)
				    *bytesToModify = remainingBytes;
            }
            if (rw == WRITE && root[i].firstBlock == FAT_EOC && fatFree != 0) { // Empty file with no associated data blocks
                for (int j = 0; j <  superblock->numDataBlocks; j++) {
                    if (fat[j] == 0) {
                        root[i].firstBlock = j;
                        break;
                    }
                }
            }
			return superblock->data + root[i].firstBlock + (fileDescriptors[fd].offset / BLOCK_SIZE);
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

    // Read the superblock, FAT blocks, and root block
    superblock = initSuperblock();
    block_read(0, (void*)superblock);
    if (memcmp(superblock->signature, "ECS150FS", 8)) { // Check signature of file system
       return -1;
    }

    fat = (uint16_t*)malloc(superblock->numDataBlocks*sizeof(uint16_t));
    for (int i = 1; i < superblock->root; i++) {
        block_read(i, ((void*)fat) + BLOCK_SIZE*(i - 1));
    }

    block_read(superblock->root, (void*)root);

    // Count available entries in FAT and in root
    for (int i = 0; i < superblock->numDataBlocks; i++) {
        if(fat[i] == 0) {
            fatFree++;
        }
    }

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
    block_write(superblock->root, (void*)root);

    // Free allocated memory
    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        free(fileDescriptors[k].filename);
    }

    free(fat);
    
    // Close the disk
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
	return 0;
}

int fs_create(const char *filename)
{
    // Don't create if no more space on disk or @filename is invalid
    if (rootFree == 0 || strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0')
        return -1;

    // Don't create file if a file with the same name already exists on the FS
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename))
            return -1;
    }

    // Create empty file and add root entry
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(root[i].filename[0] == 0) { // Find the first empty root entry
            strcpy(root[i].filename, filename);
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            block_write(superblock->root, (void*) root); // Write change to disk
            return 0;
        }
    }
 
    // Function should not reach here if file successfully created
	return -1; 
}

int fs_delete(const char *filename)
{
    void* bounceBuffer = NULL;

    // Check if @filename is valid
    if (strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0')
        return -1;

    // Don't delete the file if it is open
    for (int k = 0; k < FS_OPEN_MAX_COUNT; k++) {
        if(fileDescriptors[k].filename && !strcmp(fileDescriptors[k].filename, filename))
            return -1;
    }

    bounceBuffer = malloc(BLOCK_SIZE); 

   // Reset the associated fat entries, data blocks, and root entry
   for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename)) {
            int clearIndex = root[i].firstBlock;
            while(clearIndex != FAT_EOC) {
                block_read(superblock->data + clearIndex, bounceBuffer);
                memset(bounceBuffer, 0, BLOCK_SIZE);
                block_write(superblock->data + clearIndex, bounceBuffer);
                int next = fat[clearIndex];
                fat[clearIndex] = 0;
                fatFree++;
                clearIndex = next;
            }
            free(bounceBuffer);
            root[i].filename[0] = 0;
            root[i].size = 0;
            root[i].firstBlock = FAT_EOC;
            block_write(superblock->root, (void*) root);
            rootFree++;
            return 0;
        }
    }

    // Function should not reach here if deletion successful
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

    // Don't open if disk cannot open any more files or @filename is invalid
    if (numOpen == FS_OPEN_MAX_COUNT || strlen(filename) > FS_FILENAME_LEN || filename[strlen(filename)] != '\0') 
       return -1;
 
    // Check if the file exists on the disk 
    for(int i = 0; i <= FS_FILE_MAX_COUNT; i++) {
        if(!strcmp(root[i].filename, filename)){ // File found
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
            fileDescriptors[k].fd = k; // Given file descriptor is simply the index in fileDescriptors
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
    // Check if @fd is valid and file with @fd is open
    if (fd  >= FS_OPEN_MAX_COUNT || fd < 0 || fileDescriptors[fd].fd != fd) {
        return -1;
    }

    // Reset associated fileDescriptors entry
    memset(fileDescriptors[fd].filename, 0, FS_FILENAME_LEN);
    fileDescriptors[fd].fd = -1;
    fileDescriptors[fd].offset = -1;
    return 0;
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
}

int fs_write(int fd, void *buf, size_t count)
{
    int* bytesToWrite = (int *)malloc(sizeof(count));
    int writeBlock, blockBytes, blockOffset, bytesWritten = 0;
    void* bounceBuffer = malloc(BLOCK_SIZE);
    if (fd >= FS_OPEN_MAX_COUNT || fd < 0 || fileDescriptors[fd].fd != fd) {
            return -1;
    }

    *bytesToWrite = count;
    writeBlock = find_dataBlock(fd, bytesToWrite, WRITE);
    blockOffset = fileDescriptors[fd].offset - (fileDescriptors[fd].offset/BLOCK_SIZE)*BLOCK_SIZE;

    // Stop if no more space on disk to write
    if (writeBlock == FAT_EOC)
        return bytesWritten;

    if (blockOffset != 0) { // Offset is in middle of a block
        block_read(writeBlock, bounceBuffer);
        blockBytes = (fileDescriptors[fd].offset/BLOCK_SIZE + 1)*BLOCK_SIZE - fileDescriptors[fd].offset; // Bytes that can be written in first block
        if (*bytesToWrite < blockBytes) { // Only need to write to this one block
            memcpy(bounceBuffer + blockOffset, buf, *bytesToWrite); 
            bytesWritten += *bytesToWrite; 
            *bytesToWrite -= *bytesToWrite; // bytesToWrite = 0
            block_write(writeBlock, bounceBuffer);
        }
        else { // Write to the end of the block
            memcpy(bounceBuffer + blockOffset, buf, blockBytes);
            bytesWritten += blockBytes;
            *bytesToWrite -= blockBytes;
            buf += blockBytes;
            block_write(writeBlock, bounceBuffer);
            writeBlock++;
            memset(bounceBuffer, 0 , BLOCK_SIZE); // Clear the bounce buffer for possible later use
        }
    }

    // Write to whole blocks until no longer possible
    while (*bytesToWrite >= BLOCK_SIZE) {
        block_read(writeBlock, bounceBuffer);
        memcpy(bounceBuffer, buf, BLOCK_SIZE);
        *bytesToWrite -= BLOCK_SIZE;
        bytesWritten += BLOCK_SIZE;
        buf += BLOCK_SIZE;
        block_write(writeBlock, bounceBuffer);
        memset(bounceBuffer, 0, BLOCK_SIZE);
        // Allocate more data blocks while needed
        if (fat[writeBlock - superblock->data] == FAT_EOC && bytesToWrite > 0) {
            if (fatFree == 0) // There is no more space on the disk to write to
                return bytesWritten;
            else { // Find the first free fat block and add it to the file's chainmap
                for (int i = 0; i < superblock->numDataBlocks; i++) {
                    if(fat[i] == 0) {
                        fat[writeBlock - superblock->data] = i;
                        fat[i] = FAT_EOC;
                        fatFree--;
                        break; 
                    }
                }
            }
        }
        writeBlock++;
    }

    // Write any remaining bytes to last block
    if (*bytesToWrite > 0) {
        block_read(writeBlock, bounceBuffer);
        memcpy(bounceBuffer, buf, *bytesToWrite);
        bytesWritten += *bytesToWrite;
        *bytesToWrite -= *bytesToWrite;
        block_write(writeBlock, bounceBuffer);
    }   

    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    	if(!strcmp(root[i].filename, fileDescriptors[fd].filename) && fileDescriptors[fd].offset + bytesWritten > root[i].size) {
            root[i].size = fileDescriptors[fd].offset + bytesWritten;
            break;
        }
    }           
	
    free(bounceBuffer);
    free(bytesToWrite);

    fileDescriptors[fd].offset += bytesWritten;
    buf -= bytesWritten;

	return bytesWritten;
}

int fs_read(int fd, void *buf, size_t count)
{
    int* bytesToRead = (int *)malloc(sizeof(count));
    int readBlock, blockBytes, blockOffset, bytesRead = 0;
    void* bounceBuffer = NULL;
    if (fd >= FS_OPEN_MAX_COUNT || fd < 0 || fileDescriptors[fd].fd != fd) {
            return -1;
    }

    *bytesToRead = count;
    readBlock = find_dataBlock(fd, bytesToRead, READ);
    blockOffset = fileDescriptors[fd].offset - (fileDescriptors[fd].offset/BLOCK_SIZE)*BLOCK_SIZE;
 
    if (blockOffset != 0) { // Offset is in middle of a block
        bounceBuffer = malloc(BLOCK_SIZE);
        block_read(readBlock, bounceBuffer);
        blockBytes = (fileDescriptors[fd].offset/BLOCK_SIZE + 1)*BLOCK_SIZE - fileDescriptors[fd].offset; // Bytes to read from first block
        if (*bytesToRead < blockBytes) { // Only need to read from this one block
            memcpy(buf, bounceBuffer + blockOffset, *bytesToRead);
            *bytesToRead -= *bytesToRead; // bytesToRead = 0
            bytesRead += *bytesToRead;
        }
        else { // Read to the end of the block
            memcpy(buf, bounceBuffer + blockOffset, blockBytes);
            readBlock++;
            bytesRead += blockBytes;
            *bytesToRead -= blockBytes;
            buf += blockBytes;
            memset(bounceBuffer, 0 , BLOCK_SIZE); // Clear the bounce buffer for possible later use
        }
    }

    // Read whole blocks until no longer possible
    while (*bytesToRead >= BLOCK_SIZE) {
        block_read(readBlock, buf);
        *bytesToRead -= BLOCK_SIZE;
        bytesRead += BLOCK_SIZE;
        buf += BLOCK_SIZE;
        readBlock++;
    }

    // Read any remaining bytes
    if (*bytesToRead > 0) {
        if (!bounceBuffer)
            bounceBuffer = malloc(BLOCK_SIZE);
        block_read(readBlock, bounceBuffer);
        memcpy(buf, bounceBuffer, *bytesToRead);
        bytesRead += *bytesToRead;
        *bytesToRead -= *bytesToRead;
    }   

    if (bounceBuffer)
        free(bounceBuffer);
  
    free(bytesToRead);
    fileDescriptors[fd].offset += bytesRead;
    buf -= bytesRead; 
	return bytesRead;
}

