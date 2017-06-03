# ECS 150: Project #4 - File System
## Phase 0: The ECS-150FS specification

* The layout

We created a struct for the superblock, a struct for each entry of the
root directory where the root directory will be an array of this struct,
and represented the FAT with a pointer to uint16_t that will be dynamically
allocated and used as an array.

## Phase 1: Mounting/unmounting

* Mounting

To open the disk we call `block_disk_open(diskname)`. Then, before mounting,
we must initialize the superblock, fat blocks, and root block. We defined
a separate initialization function to handle the superblock's initialization.
For the fat blocks, we malloced the correct size by utilizing `numDataBlocks`
from the superblock. Then we go through each index and read in each block
with `block_read(i, ((void*)fat) + BLOCK_SIZE*(i - 1))`. For the root, we
read in the index of the root block which is obtained by `superblock->root`
by using `block_read(superblock->root, (void*)root)`. Then we use a for
loop to calculate the number of available entrees in the FAT and root.
It is also here that we initialize the file descriptor struct to hold
the necessary data associated with a file descriptor (the filename,
the file descriptor number, and the offset).
We then wrote `fs_info()` with print statements to the appropriate values
we just obtained from reading in the blocks from the disk.

* Unmounting

We call `block_write(superblock->root, (void*)root)` to write the current
files and data out to disk. We then free memory we allocated for file
descriptors and the fat, before finally closing the disk with
`block_disk_close()`.

* Testing: fs_info()

For testing, at this point, `fs_info()` output corresponds correctly with the
reference program. We had a few issues with freeing FAT's allocated memory in
unmount, but found out we were allocating too much when we were incrementing
the pointer for each iteration of the for loop.

## Phase 2: File creation/deletion

* Creation

We find an empty entry in the root directory by iterating through the root
array and looking for one where the first character of the filename is 0,
which means the entry is empty. We then initialize the newly created fileh
and `block_write(superblock->root, (void*) root)` the change to disk.

* Deletion

We look for the associated file in the root directory by comparing filename
with root[i].filename. Once that is done, we reset the associated fat entries,
data blocks, and root entry by reading from disk the data block we want to
delete into a buffer, clearing the memory with
`memset(bounceBuffer, 0, BLOCK_SIZE)`, and writing the changes to disk.
We then reset the fat entry and root entry by setting the appropriate values
to each of the associated members. We don't forget to increment the counter
for `fatFree` and `rootFree`.

* Testing: fs_ls()

We matched the output by going through each of the root entries and printing
the associated data if the root entry isn't empty.

## Phase 3: File Descriptor operations

* File Descriptors

Our file descriptor array holds a max number of 32 file descriptors. To allow
for the filename to be open multiple times with unique file descriptors, we
do not check for the filename being in use, and only check for unused `fd`s.
We represented file descriptors with a struct that contains the `filename`,
the `fd`, and the `offset`.

* Open

One caveat here was that when we were checking if the file exists on disk and
returning -1 if it doesn't, we had to check all values and return at the end
outside the for loop by checking a flag we created for whether or not the file
exists.
To open a file descriptor, we assign a file descriptor entry to the associated
filename. We decided to make `fd` be -1 to represent an unused file descriptor
so we look for the first available file descriptor and assign it its* 
appropriate index.

* Close

We close the file by doing the opposite of Open: we memset the filename and
set the associated `fd` and `offset` to -1.

* Testing: fs_stat()

`fs_stat()` searches the root directory for the associated `fd` in the file
descriptor array that matches `filename` and returns the size. `fs_lseek`
sets the `offset` of the given `fd`'s `offset`.

## Phase 4: File reading/writing

* Helper Functions

We wrote a function `find_dataBlock` to find the index of the data block
that the offset leads us to. Because we didn't want to reuse the same loop,
we had this function also set `bytesToModify` to be the number of bytes
we will actually read in case the specified `count` is greater than the 
number of bytes until the end of file.

We did not write a function that allocates a new data block and links it
to the end of the file's data block chain because our implementation only
uses this loop once.

* Read

For read and write, we mapped out the logic on paper before translating
into code. After we find the block we are reading from with our helper
function and find how many bytes we are actually reading, we check if
the offset is in the middle of the block. Then we check if this is the
only block we are reading from. If so, we just read to the buffer normally.
Otherwise, we read to the end of the block and prepare the `bounceBuffer`
for later use. We then read entire blocks with `block_read()` until no
longer possible. After that, we read any remaining bytes that need to be
read by loading the last block into the `bounceBuffer` and reading
the remaining bytes from the `bounceBuffer` into the user-supplied buffer.
We finally free the memory we allocated for `bounceBuffer` and `bytesToRead`
and increment the file descriptor offset before returning the number of
bytes read.

* Write

`fs_write()` uses the same logic as `fs_read()` to check the possible
"mismatch" cases. The difference here is that we always try to write count
unless there is no space on disk available. As we write entire blocks,
we check if we must allocate more data blocks to continue writing. If so,
we iterate through the datablocks and check if it's associated FAT entry
is 0 which means it's free. We then assign the previous last FAT entry of
the file the index of the next free FAT entry that we just found. We also
check if there is no more space on disk by checking if there are no more
free FAT blocks to add to the chainmap. If so, we just return the number
of bytes we have written so far. The next block of code occurs if we
still have bytes to write in the last block of the file, and writes
by reading in the data to `bounceBuffer`, `memcpy`ing to it, and then
writing the data in the `bounceBuffer` back to disk. We finish by updating
the file size, freeing allocated memory, incrementing the offset, and
returning the number of bytes written.
