#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h> //Integers
#include "disk.h"
#include "fs.h"


//in header
/** Maximum filename length (including the NULL character) */
// #define FS_FILENAME_LEN 16

/** Maximum number of files in the root directory */
// #define FS_FILE_MAX_COUNT 128 // entry

/** Maximum number of open files */
// #define FS_OPEN_MAX_COUNT 32

// #define FS_NAME "ECS150FS"
const static char FS_NAME[8] = "ECS150FS";

/* Superbock
* Holds statistics about the filesystem 
* according to the requirment 8 + 2 + 2 + 2 + 2 + 1 + 4079 = 4096; one block
*/
struct SuperBlock 
{
	uint64_t signature; // 8 bytes, "ECS150FS"
	uint16_t amount_blk; // Total amount of blocks of virtual disk 
	uint16_t rootdir_blk_idx; // Root directory block index
	uint16_t data_blk_idxs; // Data block start index
	uint16_t amount_data_blk; // Amount of data blocks
	uint8_t  num_blk_fat; // Number of blocks for FAT
    // 4079 Unused/Padding
};


struct RootDir {              // Inode structure
    char filename[FS_FILENAME_LEN];         // Whether or not inode is valid
    uint32_t Size;          // Size of file
    uint16_t first_blk_idx; // Direct pointers
    //10 byte Unused/Padding
};

// struct Fat
// {
// 	uint16_t next;
// };

union Block {
    SuperBlock  super;              // Superblock
    uint16_t    fat[BLOCK_SIZE/2];        
    RootDir     rootdir[FS_FILE_MAX_COUNT];   // Pointer block
    char        block[BLOCK_SIZE];     // Data block
};

/* TODO: Phase 1 */

/**
 * fs_mount - Mount a file system
 * @diskname: Name of the virtual disk file
 *
 * Open the virtual disk file @diskname and mount the file system that it
 * contains. A file system needs to be mounted before files can be read from it
 * with fs_read() or written to it with fs_write().
 *
 * Return: -1 if virtual disk file @diskname cannot be opened, or if no valid
 * file system can be located. 0 otherwise.
 */
int fs_mount(const char *diskname)
{
	/* TODO: Phase 1 */
	if (block_disk_open(diskname) != 0) return -1;
	

	return 0;
}

/**
 * fs_umount - Unmount file system
 *
 * Unmount the currently mounted file system and close the underlying virtual
 * disk file.
 *
 * Return: -1 if no underlying virtual disk was opened, or if the virtual disk
 * cannot be closed, or if there are still open file descriptors. 0 otherwise.
 */
int fs_umount(void)
{
	/* TODO: Phase 1 */
}

/**
 * fs_info - Display information about file system
 *
 * Display some information about the currently mounted file system.
 *
 * Return: -1 if no underlying virtual disk was opened. 0 otherwise.
 */
int fs_info(void)
{
	/* TODO: Phase 1 */
}

/**
 * fs_create - Create a new file
 * @filename: File name
 *
 * Create a new and empty file named @filename in the root directory of the
 * mounted file system. String @filename must be NULL-terminated and its total
 * length cannot exceed %FS_FILENAME_LEN characters (including the NULL
 * character).
 *
 * Return: -1 if @filename is invalid, if a file named @filename already exists,
 * or if string @filename is too long, or if the root directory already contains
 * %FS_FILE_MAX_COUNT files. 0 otherwise.
 */
int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
}

/**
 * fs_delete - Delete a file
 * @filename: File name
 *
 * Delete the file named @filename from the root directory of the mounted file
 * system.
 *
 * Return: -1 if @filename is invalid, if there is no file named @filename to
 * delete, or if file @filename is currently open. 0 otherwise.
 */
int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
}

/**
 * fs_ls - List files on file system
 *
 * List information about the files located in the root directory.
 *
 * Return: -1 if no underlying virtual disk was opened. 0 otherwise.
 */
int fs_ls(void)
{
	/* TODO: Phase 2 */
}

/**
 * fs_open - Open a file
 * @filename: File name
 *
 * Open file named @filename for reading and writing, and return the
 * corresponding file descriptor. The file descriptor is a non-negative integer
 * that is used subsequently to access the contents of the file. The file offset
 * of the file descriptor is set to 0 initially (beginning of the file). If the
 * same file is opened multiple files, fs_open() must return distinct file
 * descriptors. A maximum of %FS_OPEN_MAX_COUNT files can be open
 * simultaneously.
 *
 * Return: -1 if @filename is invalid, there is no file named @filename to open,
 * or if there are already %FS_OPEN_MAX_COUNT files currently open. Otherwise,
 * return the file descriptor.
 */
int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
}

/**
 * fs_close - Close a file
 * @fd: File descriptor
 *
 * Close file descriptor @fd.
 *
 * Return: -1 if file descriptor @fd is invalid (out of bounds or not currently
 * open). 0 otherwise.
 */
int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

/**
 * fs_stat - Get file status
 * @fd: File descriptor
 *
 * Get the current size of the file pointed by file descriptor @fd.
 *
 * Return: -1 if file descriptor @fd is invalid (out of bounds or not currently
 * open). Otherwise return the current size of file.
 */
int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

/**
 * fs_lseek - Set file offset
 * @fd: File descriptor
 * @offset: File offset
 *
 * Set the file offset (used for read and write operations) associated with file
 * descriptor @fd to the argument @offset. To append to a file, one can call
 * fs_lseek(fd, fs_stat(fd));
 *
 * Return: -1 if file descriptor @fd is invalid (out of bounds or not currently
 * open), or if @offset is out of bounds (beyond the end of the file). 0
 * otherwise.
 */
int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

/**
 * fs_write - Write to a file
 * @fd: File descriptor
 * @buf: Data buffer to write in the file
 * @count: Number of bytes of data to be written
 *
 * Attempt to write @count bytes of data from buffer pointer by @buf into the
 * file referenced by file descriptor @fd. It is assumed that @buf holds at
 * least @count bytes.
 *
 * When the function attempts to write past the end of the file, the file is
 * automatically extended to hold the additional bytes. If the underlying disk
 * runs out of space while performing a write operation, fs_write() should write
 * as many bytes as possible. The number of written bytes can therefore be
 * smaller than @count (it can even be 0 if there is no more space on disk).
 *
 * Return: -1 if file descriptor @fd is invalid (out of bounds or not currently
 * open). Otherwise return the number of bytes actually written.
 */
int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

/**
 * fs_read - Read from a file
 * @fd: File descriptor
 * @buf: Data buffer to be filled with data
 * @count: Number of bytes of data to be read
 *
 * Attempt to read @count bytes of data from the file referenced by file
 * descriptor @fd into buffer pointer by @buf. It is assumed that @buf is large
 * enough to hold at least @count bytes.
 *
 * The number of bytes read can be smaller than @count if there are less than
 * @count bytes until the end of the file (it can even be 0 if the file offset
 * is at the end of the file). The file offset of the file descriptor is
 * implicitly incremented by the number of bytes that were actually read.
 *
 * Return: -1 if file descriptor @fd is invalid (out of bounds or not currently
 * open). Otherwise return the number of bytes actually read.
 */
int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}
