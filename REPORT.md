# ECS 150: Project #4 - File System
## Phase 0: The ECS-150FS specification

* The layout

We created a struct for the superblock, a struct for each entry of the
root directory where the root directory will be an array of this struct,
and represented the FAT with a pointer to uint16_t that will be dynamically
allocated and used as an array.

## Phase 1: Mounting/unmounting

* Phase 2.1: Unprotected TPS with naive cloning

`tps_init()` only created the TPS queue to hold all the Threads' TPSes.
We used `queue_iterate()` whenever we wanted to see if a TPS exists for a
thread and whenever we wanted to find a specific thread's TPS.
For this phase, we initially used `mmap` with protect parameter
`PROT_READ | PROT_WRITE` to allocate a page of memory to the unprotected TPS.
`tps_clone()` did naive cloning by creating a `sourceTPS` to store the source
TPS. We then used `tps_create()` to create a new `curTPS` and call memcpy on 
the source TPS's page to create a direct copy into `curTPS`.
We also added critical sections to `tps_create()`, `tps_destroy()`,
`tps_read()`, `tps_write()`, and `tps_clone()` because they modify a global
`curTPS` variable that refers to the current running thread's TPS.

* Testing

For testing, at this point, tps.x worked and should continue to work
throughout the rest of this phase.

* Phase 2.2: Protected TPS

To add TPS protection, in `tps_create()`, we modified the `mmap` function
protect parameter to be `PROT_NONE` to have no read/write permisson by default.
In `tps_read()` and `tps_write()`, we used `mprotect` on the page to modify
this parameter and allow reads and writes temporarily with PROT_READ and
PROT_WRITE respectively. After the page is accessed, `mprotect` sets the
protect parameter back to PROT_NONE.
This gives birth to a new source for segfaults that occur whenever a thread
tries to access a TPS area that is protected. To differentiate this from other
segfaults, we defined a segfault handler that iterates with `queue_iterate()`
and `find_page()` to find if the address where the fault occured matches one
of the addresses of the beginning of the TPS areas. If there is a match, it
prints an error message to stderr and transmits the signal again to cause the
program to crash. This signal catching is set up in `tps_init()`.

* Testing

For testing, we deliberately commented out the mprotect functions in
`tps_read()` and `tps_write()` to cause a segfault from accessing protected
TPS areas. This caused tps.x to output as expected:
```
thread2: read OK!
thread1: read OK!
TPS protection error!
segmentation fault (core dumped)
```

* Phase 2.3: Copy-on-Write cloning

We added another level of indirection by replacing the pointer to the page in
the tps object with a pointer to a struct that contains a pointer to the page.
This new struct also contains a count that refers to how many TPSes are
sharing it. we create this new page struct In `tps_create()`.
Because we don't want to create a new page in `tps_clone()`, we had to, instead of
calling `tps_create()`, give `curTPS` the address of `sourceTPS`'s page and then
increase the count for that page.
In `tps_write()`, if the page the `curTPS` is referring to has a count greater
than 1, we must allocate memory and create a new page for the calling thread to
write to so that it doesn't change the contents of the page for other threads.
When cloning the contents of the page, we must also use mprotect to temporarily
allow the original page to be read from and the new page to be written to. The
new page's count is set to 1 and we make the `curTPS->mempage` now refer to the
new page.

* Testing

For testing, we used ddd and printed the addresses of the pages after we cloned
the page to make sure they had the same addresses. We then printed the addresses
of the pages after writing to make sure if the page was shared, the current
thread wrote to a new page with a different address.
