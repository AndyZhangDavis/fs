#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strcmp, strlen, strcpy

#include <stdbool.h>
#include <stdint.h> //Integers

#include "disk.h"
#include "fs.h"



//in header

#define eprintf(format, ...) \
    fprintf (stderr, format, ##__VA_ARGS__)
#define oprintf(format, ...) \
    fprintf (stdout, format, ##__VA_ARGS__)
#define clamp(x, y) (((x) <= (y)) ? (x) : (y))
#define FAT_EOC 0xFFFF
const static char FS_NAME[8] = "ECS150FS";


/******************* Data Structure *********************/
/* Metadata format
* Superbock
* Holds statistics about the filesystem 
* according to the requirment 8 + 2 + 2 + 2 + 2 + 1 + 4079 = 4096; one block

Offset  Length (bytes)  Description
0x00    8   Signature (must be equal to “ECS150FS”)
0x08    2   Total amount of blocks of virtual disk
0x0A    2   Root directory block index
0x0C    2   Data block start index
0x0E    2   Amount of data blocks
0x10    1   Number of blocks for FAT
0x11    4079    Unused/Padding

* FAT
The FAT is a flat array, possibly spanning several blocks, which entries are composed of 16-bit unsigned words. There are as many entries as data blocks in the disk.

* Root Directory 
Offset  Length (bytes)  Description
0x00    16  Filename (including NULL character)
0x10    4   Size of the file (in bytes)
0x14    2   Index of the first data block
0x16    10  Unused/Padding
*/

struct SuperBlock 
{
    char     signature[8];
    uint16_t total_blk_count;  // Total amount of blocks of virtual disk 
    uint16_t rdir_blk;         // Root directory block index
    uint16_t data_blk;         // Data block start index
    uint16_t data_blk_count;   // Amount of data blocks
    uint8_t  fat_blk_count;    // Number of blocks for FAT

    uint16_t  fat_used;     // myself
    uint16_t  rdir_used;    

    char     unused[4063];     // 4079 Unused/Padding, I use 32bits
}__attribute__((packed));


struct RootDirEntry {                // Inode structure
    char        filename[FS_FILENAME_LEN];         // Whether or not inode is valid
    uint32_t    file_sz;          // Size of file
    uint16_t    first_data_blk; // Direct pointers

    uint16_t    last_data_blk; // Direct pointers
    uint8_t     open;
    char        unused[7];     // one char for indicating writing 'w'
}__attribute__((packed));

struct FileDescriptor
{
    void * file_entry;
    size_t offset;
};


/******************* Global Var *********************/
char * disk = NULL; //virtual disk name pointer
struct SuperBlock * sp = NULL;  // superblock pointer
void * root_dir = NULL;         // root directory pointer
struct RootDirEntry * dir_entry = NULL; // 32B * 128 entry, file entry pointer
void * fat = NULL;              //FAT block pointer
uint16_t * fat16 = NULL;        //fat array entry pointer

int fd_cnt = 0;     // fd used number
struct FileDescriptor* filedes[FS_OPEN_MAX_COUNT];


/******************* helper function*********************/

/* used to check the validation of the input file descirptor number */
bool is_valid_fd(int fd){
    if(fd < 0 || fd >= FS_OPEN_MAX_COUNT || filedes[fd] == NULL) 
        return false;
    else return true;
}

/* get valid file descirptor number */
int get_valid_fd(){
    if(filedes == NULL || fd_cnt >= FS_OPEN_MAX_COUNT){
        // eprintf("get_valid_fd: fail\n");
        return -1;
    }
    for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
        if(filedes[i] == NULL)
            return i;

    // eprintf("get_valid_fd: no available file descirptor\n");
    return -1;
}

/* get file directory entry pointer according to id 
 * @id, index of the file in root directory entry
 * return the address of the entry; return NULL if fail
*/
struct RootDirEntry * get_dir(int id){
    if(root_dir == NULL) return NULL;
    return (struct RootDirEntry *)(root_dir + id * sizeof(struct RootDirEntry));
}

/* block index -> fat entry address
 * get file directory entry pointer according to block id 
*/
uint16_t * get_fat(int id){
    if(fat == NULL || id == 0xFFFF) return NULL;
    if(id < 0 || id >= sp->data_blk_count){
        // eprintf("get_fat: @block_id out of boundary\n");
        return NULL;
    }

    return (uint16_t *)(fat + 2 * id);
    // return (uint16_t *)(fat + sizeof(uint16_t) * id);
}

/* get next free data block
 */
int32_t get_free_blk_idx(){
    if(fat == NULL || sp == NULL)
        return -1;
    if(sp->data_blk_count - sp->fat_used == 0) {
        // eprintf("data blk exhausted\n");
        return -1;
    }       

    uint16_t i = 1;
    uint16_t * tmp = fat;
    tmp++; // skip #0 fat
    // for (tmp = fat; i < sp->fat_blk_count * BLOCK_SIZE / 2 ; ++i, tmp += sizeof( uint16_t ))
    // for (; i < sp->fat_blk_count * BLOCK_SIZE / 2 ; ++i, tmp++)
    for (; i < sp->data_blk_count ; ++i, tmp++)
        if (*tmp == 0 )
            return (int32_t)i;
    // if( i == sp->fat_blk_count * BLOCK_SIZE / 2)
        
    // if( i == sp->data_blk_count)
        // eprintf("fat exhausted\n");

    return -1;
}

/* calculate how many blocks needed for a file of size @sz */
int file_blk_count(uint32_t sz){
    if(sz == 0) return 1;
    
    int k = sz / BLOCK_SIZE;
    if( k * BLOCK_SIZE < sz)
        return k + 1;
    else return k;
}

/* get next free file directory entry index;
 * check the duplicated existed filename by @filename
 * return index number; -1 if fail. set the entry_ptr address 
 */
int get_valid_directory_entry(const char * filename, void *  entry_ptr){
    if(filename == NULL || root_dir == NULL || sp == NULL)
        return -1;
    int i;
    for (i = 0; i < FS_FILE_MAX_COUNT; ++i)
    {
        // struct RootDirEntry * tmp = root_dir + i * sizeof(struct RootDirEntry);
        struct RootDirEntry * tmp = get_dir(i);
        if(strcmp(tmp->filename, filename) == 0){
            // eprintf("get_valid_directory_entry: @filename already exists error\n");
            return -1;
        }
        if(tmp->filename[0] == 0)
            break;
    }
    if(i == FS_FILE_MAX_COUNT){
        // eprintf("fs_create: root directory full error\n");
        return -1;
    }

    if(entry_ptr)
        entry_ptr = root_dir + i * sizeof(struct RootDirEntry);
    return i;
}

/* get the dir entry id by filename*/
int get_directory_entry(const char * filename, void *  entry_ptr){
    if(filename == NULL || root_dir == NULL || sp == NULL)
        return -1;
    int i;
    for (i = 0; i < FS_FILE_MAX_COUNT; ++i)
    {
        // struct RootDirEntry * tmp = root_dir + i * sizeof(struct RootDirEntry); 
        struct RootDirEntry * tmp = get_dir(i);

        if(strcmp(tmp->filename, filename) == 0){
            if(entry_ptr)
                entry_ptr = tmp;
            return i;
        }
    }
    if(i == FS_FILE_MAX_COUNT){
        // eprintf("get_directory_entry: not found\n");
        return -1;
    }

    return -1;
}

/* resume the fat as zero ( free ) again
 * update the sp->fat_used
*/
int erase_fat(uint16_t * id){ // recursion to erase
    if(sp == NULL || root_dir == NULL || id == NULL)
        return -1;

    while((*id) != 0xFFFF){
        uint16_t next = *id;
        *id = 0;
        sp->fat_used -= 1;
        id = get_fat(next);
    }
    *id = 0;
    sp->fat_used -= 1;

    return 0;
}


/**************** helper-II for @fs_mount, @fs_umount and etc. *************/

void sp_setup(){
    if(sp == NULL || root_dir == NULL)
        return ;

    dir_entry = root_dir;
    sp->fat_used = 1;
    sp->rdir_used = 0;
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i, dir_entry++)
    {
        if(dir_entry->filename[0] != 0){
            sp->rdir_used += 1;

            int tmp = file_blk_count(dir_entry->file_sz);
            if(dir_entry->first_data_blk != FAT_EOC)
                sp->fat_used += tmp;
        }
    }
}

/* write back meta-information
 helper-II for @fs_umount and etc. 
 * also update the meta-information when write, delete a file
*/
int write_meta(){

    if(sp->fat_used >= sp->data_blk_count)
        sp->fat_used = sp->data_blk_count;
    if(sp == NULL || root_dir == NULL || fat == NULL){
        // eprintf("no virtual disk mounted to write_meta");
        return -1;
    }
    if(block_write(0, (void *)sp) < 0)
    {
        // eprintf("fs_umount write back sp error\n");
        return -1; 
    }
    if(block_write(sp->rdir_blk, root_dir) < 0)// write back
    {
        // eprintf("fs_umount write back dir error\n");
        return -1; 
    }
    for (int i = 0; i < sp->fat_blk_count; ++i)
    {
        if(block_write(1 + i, fat + BLOCK_SIZE * i) < 0)// write back
        {
            // eprintf("fs_umount write back fat blk %d error\n", i);
            return -1; 
        }
    }
    return 0;
}
/*
 * free space to sp, root_dir, and fat; set to zero for all of them
 * fail return -1; succeed return 0;
*/
void clear(){
    if(sp) {
        free(sp);
        sp = NULL;
    }
    if(root_dir){
        free(root_dir);
        root_dir = NULL;
    }
    if(fat)
    {
        free(fat);
        fat = NULL;
    }

    if(disk) free(disk);
    disk = NULL;
    dir_entry = NULL;
    fat16 = NULL;
    fd_cnt = 0;

    for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
    {
        if(filedes[i] != NULL) // should n't happen actually
        {
            free(filedes[i]);
            filedes[i] = NULL;
            // eprintf("alert file descriptor %d is not clear\n",i);
        }
    }

}


/*
 * alloc space to sp, root_dir, and fat; set to zero for all of them
 * initialize filedes, fd_cnt
 * initialize sp_setup()
 * fail return -1; succeed return 0;
*/
int init_alloc(){


    sp = malloc(BLOCK_SIZE); 
    // sp = calloc(BLOCK_SIZE,1); 
    if(sp == NULL) {
        clear();
        return -1;
    }

    root_dir = malloc(BLOCK_SIZE);
    // root_dir = calloc(BLOCK_SIZE,1);
    if(root_dir == NULL) {
        clear();
        return -1;
    }

    fat = malloc(BLOCK_SIZE * sp->fat_blk_count);
    if(fat == NULL) {
        clear();
        return -1;
    }

    for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
        filedes[i] = NULL;
    fd_cnt = 0;

    memset(sp, 0, BLOCK_SIZE);
    memset(root_dir, 0, BLOCK_SIZE);
    memset(fat, 0, BLOCK_SIZE * sp->fat_blk_count);    
    
    // dir_entry = get_dir(0);  

    return 0;   
}

/** version 1. 0
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
    if (block_disk_open(diskname) != 0) return -1;
    
    // if(init_alloc() < 0) return -1; // allocate error

    sp = malloc(BLOCK_SIZE); 
    if(sp == NULL) { clear(); return -1; }

    memset(sp, 0, BLOCK_SIZE);
    if(block_read(0, (void *)sp) < 0) { clear(); return -1; }
    if(strncmp(sp->signature, FS_NAME, 8) != 0){
        clear();
        // eprintf("fs_mount: non ECS150FS file system\n");
        return -1;
    }
    if(block_disk_count() != sp->total_blk_count)
    {
        clear();
        // eprintf("fs_mount: incorrect block size in metadata\n");
        return -1;
    }
    if(sp->data_blk_count > (sp->fat_blk_count * BLOCK_SIZE / 2))
    {
        clear();
        // eprintf("fs_mount: fat size and block size dismatch\n");
        return -1;
    }

    root_dir = malloc(BLOCK_SIZE);
    if(root_dir == NULL) { clear(); return -1; }
    memset(root_dir, 0, BLOCK_SIZE);
    if(block_read(sp->rdir_blk, root_dir) < 0){
        // eprintf("fs_mount: read root dir error\n");
        clear(); 
        return -1; 
    }

    dir_entry = get_dir(0);
    sp_setup();

    fat = malloc(BLOCK_SIZE * sp->fat_blk_count);
    if(fat == NULL) { clear(); return -1; }
    memset(fat, 0, BLOCK_SIZE * sp->fat_blk_count);
    for (int i = 0; i < sp->fat_blk_count; ++i)
    {
        if(block_read(i+1, fat + BLOCK_SIZE * i) < 0){
            // eprintf("fs_mount: read %d th(from 0) fat block error\n", i);
            clear();
            return -1;
        }
    }
    fat16 = get_fat(0);

    for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i)
        filedes[i] = NULL;
    fd_cnt = 0;

    disk = malloc(strlen(diskname) + 1);
    strcpy(disk, diskname);

    write_meta(); // for data used

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
    /* write back: super block, fat, dir*/
    if(sp == NULL)
        return -1;  // no underlying virtual disk was opened

    if(write_meta() < 0 ) return -1; 


    if(fd_cnt > 0){
        // eprintf("there are files open, unable to umount\n");
        return -1;
    }

    if(block_disk_close() < 0) { //cannot be closed
        // eprintf("fs_umount error\n");
        return -1; 
    }

    clear();
    return 0;
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
    if(sp == NULL || disk == NULL) {
        // eprintf("fs_info: no underlying virtual disk was mounted sucessfully\n");
        return -1;
    }
    oprintf("FS Info:\n");
    // eprintf("signature=%s\n",sp->signature); // non-terminator
    // eprintf("%.*s\n", 8, sp->signature); // works

    oprintf("total_blk_count=%d\n",sp->total_blk_count);
    oprintf("fat_blk_count=%d\n",sp->fat_blk_count);
    oprintf("rdir_blk=%d\n",sp->rdir_blk);
    oprintf("data_blk=%d\n",sp->data_blk);
    oprintf("data_blk_count=%d\n",sp->data_blk_count);

    oprintf("fat_free_ratio=%d/%d\n", (sp->data_blk_count - sp->fat_used), sp->data_blk_count);
    oprintf("rdir_free_ratio=%d/%d\n", (FS_FILE_MAX_COUNT - sp->rdir_used),FS_FILE_MAX_COUNT);

    return 0;
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
    if(sp == NULL || root_dir == NULL){
        // eprintf("fs_create: no vd mounted or root dir read\n");
        return -1;
    }
    /* @filename is invalid; or string @filename is too long*/
    if (filename == NULL || filename[0] == 0 || strlen(filename) >= FS_FILENAME_LEN ) // strlen doesn't include NULL char
    {
        // eprintf("fs_create: filaname is invalid\n");
        return -1;
    }

    int entry_id = get_valid_directory_entry(filename, NULL);
    if(entry_id < 0)
        return -1; // no valid dir entry 

    dir_entry = get_dir(entry_id); 
    strcpy(dir_entry->filename, filename);
    dir_entry->file_sz = 0;
    dir_entry->open = 0;
    dir_entry->first_data_blk = FAT_EOC; // entry, should assign block here, in case the file size is 0;
    dir_entry->last_data_blk = FAT_EOC;
    memset(dir_entry->unused, 0, 7);

    sp->rdir_used += 1; // how to deal with @setup_sp

    return 0;
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
    struct RootDirEntry * cur_entry = NULL;
    int entry_id = get_directory_entry(filename, NULL);
    if(entry_id < 0) return -1; // not found or sp, dir == NULL
    cur_entry = get_dir(entry_id);
    // or if file @filename is currently open. 0 otherwise.

    // cur_entry = root_dir + sizeof(struct RootDirEntry) * entry_id;
    // if(dir[entry_id]->open > 0)
    if(cur_entry->open > 0){
        // eprintf("fs_delete: the file is open now, uable to close\n");
        return -1;
    }

    if(cur_entry->first_data_blk != FAT_EOC){ // not empty file
        fat16 =  get_fat(cur_entry->first_data_blk);
        erase_fat(fat16); // how about return -1?
    }
    
    memset(cur_entry, 0, sizeof(struct RootDirEntry));

    sp->rdir_used -= 1;
    write_meta();

    return 0;
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
    if(sp == NULL || root_dir == NULL){
        // eprintf("no underlying virtual disk was opened\n");
        return -1;
    }

    oprintf("FS Ls:\n");

    dir_entry = get_dir(0);
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i, dir_entry++)
    {
        // dir_entry = get_dir(i);
        if((dir_entry->filename)[0] != 0){
            oprintf("file: %s, size: %d, data_blk: %d\n", dir_entry->filename, dir_entry->file_sz, dir_entry->first_data_blk);
        }
    }


    return 0;
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
    if(fd_cnt >= FS_OPEN_MAX_COUNT || filename == NULL || strlen(filename) >= FS_FILENAME_LEN)
        return -1;

    int entry_id = get_directory_entry(filename, NULL);
    if(entry_id < 0) return -1; // not found or sp, dir == NULL
    // or if file @filename is currently open. 0 otherwise.

    int fd = get_valid_fd();
    if(fd < 0) return -1;

    dir_entry = get_dir(entry_id);
    // ++(dir_entry->open); // not here

    filedes[fd] = malloc(sizeof(struct FileDescriptor));
    filedes[fd]->file_entry = dir_entry;
    filedes[fd]->offset = 0;
    if(fs_lseek(fd, 0) < 0){
        free(filedes[fd]);
        return -1;
    }
    
    ++(dir_entry->open);
    dir_entry->unused[0] = 'o';
    ++fd_cnt;

    return fd;
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
    // if(fd < 0 || fd >= FS_OPEN_MAX_COUNT || filedes[fd] == NULL)  return -1;
    if(!is_valid_fd(fd)) return -1;

    dir_entry = filedes[fd]->file_entry;
    dir_entry->open -= 1;
    dir_entry->unused[0] = 'x';

    free(filedes[fd]);
    filedes[fd] = NULL;

    fd_cnt--;
    
    return 0;
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
    if(!is_valid_fd(fd)) 
        return -1;

    dir_entry = filedes[fd]->file_entry;

    return dir_entry->file_sz;
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
    if(!is_valid_fd(fd)) return -1;

    dir_entry = (struct RootDirEntry *)(filedes[fd]->file_entry);
    if(offset > dir_entry->file_sz) return -1;

    filedes[fd]->offset = offset;

    return 0;
}


// int fs_write(int fd, void *buf, size_t count)
uint16_t get_offset_blk(int fd, size_t offset){
    if(!is_valid_fd(fd)){
        return 0; // impossible
    }

    dir_entry = filedes[fd]->file_entry;
    // if(offset == 0) return dir_entry->first_data_blk;
    if(offset >= dir_entry->file_sz){
        // eprintf("get_offset_blk fail: offset is larger than file size\n");
        return 0;
    }
    
    int no_blk = file_blk_count(offset);
    uint16_t blk = dir_entry->first_data_blk;
    while(no_blk > 1){ // need some error check
        blk = *(get_fat(blk)); //impossible NULL pointer
        no_blk -= 1;
    }

    return blk; 
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
    if(!is_valid_fd(fd)) return -1;

    struct RootDirEntry * w_dir_entry = filedes[fd]->file_entry;
    if(w_dir_entry->unused[0] == 'w'){
        // eprintf("other writing continues, unable to write\n");
        return -1;
    }
    if(count == 0) return 0;

    size_t offset = filedes[fd]->offset;
    size_t real_count = count;
    int32_t leftover_count = real_count;
    uint16_t write_blk;
    size_t expand = 0;
    uint16_t new_blk[8192];

    if(offset > w_dir_entry->file_sz)
        return -1;

    w_dir_entry->unused[0] = 'w';

    if( w_dir_entry->first_data_blk == FAT_EOC){ // empty file, no blk assign
        int32_t temp = get_free_blk_idx();
        if(temp < 0) return 0;

        w_dir_entry->first_data_blk = (uint16_t)temp;
        w_dir_entry->last_data_blk = (uint16_t)temp;
        fat16 = get_fat(w_dir_entry->first_data_blk);
        *fat16 = 0xFFFF;
        sp->fat_used += 1; // !!!!
    }

    if(offset == 0){
        write_blk = w_dir_entry->first_data_blk;
    }
    else if(offset == w_dir_entry->file_sz){
        int32_t temp = get_free_blk_idx();

        if(temp < 0) {
            w_dir_entry->unused[0] = 'n';
            return 0; // no block available
        }

        write_blk = (uint16_t) temp;
        new_blk[expand++] = write_blk;
    }
    else {
        write_blk = get_offset_blk(fd, offset);
    }

    void * bounce_buffer = calloc(BLOCK_SIZE, 1);
    int buf_idx = 0;
    if(block_read(write_blk + sp->data_blk, bounce_buffer) < 0 ){
        free(bounce_buffer);
        w_dir_entry->unused[0] = 'n';
        return 0;
    }

    buf_idx = clamp(BLOCK_SIZE - w_dir_entry->file_sz % BLOCK_SIZE, leftover_count);
    memcpy(bounce_buffer + w_dir_entry->file_sz % BLOCK_SIZE, buf, buf_idx);
    leftover_count -= buf_idx;
    if(block_write(sp->data_blk + write_blk, bounce_buffer) < 0){
        w_dir_entry->unused[0] = 'n';
        free(bounce_buffer);
        return 0;
    } // write the first blk


    
    while(leftover_count > 0){
        // get the next written block
        if(expand > 0){
            int32_t temp = get_free_blk_idx();
            if(temp < 0) 
                break; // no more block
            write_blk = (uint16_t) temp;
            new_blk[expand++] = write_blk;
        }
        else{ // 不用找新的，但是可能解下来需要找新的
            write_blk = *(get_fat(write_blk));
            if(write_blk == 0xFFFF){
                int32_t temp = get_free_blk_idx();
                if(temp < 0) 
                    break; // no more block
                write_blk = (uint16_t) temp;
                new_blk[expand++] = write_blk;
            }
        }
        //write
        if(leftover_count >= BLOCK_SIZE){
            if(block_write(sp->data_blk + write_blk, buf + buf_idx) < 0){
                w_dir_entry->unused[0] = 'n';
                free(bounce_buffer);
                return 0;
            }
            buf_idx += BLOCK_SIZE;

        }
        else{
            if(block_read(write_blk + sp->data_blk, bounce_buffer) < 0 ){
                free(bounce_buffer);
                w_dir_entry->unused[0] = 'n';
                return 0;
            }
            memcpy(bounce_buffer, buf + buf_idx, leftover_count);
            if(block_write(sp->data_blk + write_blk, bounce_buffer) < 0){
                free(bounce_buffer);
                w_dir_entry->unused[0] = 'n';
                return 0;
            }
        }
        leftover_count -= BLOCK_SIZE;
    } // while end
    
    //update the metadata
    if(leftover_count > 0) {
        real_count -= leftover_count;
    }
    if(real_count + offset > w_dir_entry->file_sz)
        w_dir_entry->file_sz = real_count + offset; 

    // printf("2222222!!!!!!!!!!!!!!!!!!!!!!!\n");
    fat16 = get_fat(w_dir_entry->last_data_blk);
    size_t i = 0;
    sp->fat_used += expand;
    while(expand > 0){
        *fat16 = new_blk[i];
        fat16 = get_fat(new_blk[i]);
        w_dir_entry->last_data_blk = new_blk[i];

        i += 1;
        expand -= 1;
    }
    *fat16 = 0xFFFF;

    // printf("3333333!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    write_meta();
    w_dir_entry->unused[0] = 'n';
    if(bounce_buffer) free(bounce_buffer);
    
    return real_count;
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
    if(!is_valid_fd(fd)) return -1;
    dir_entry = filedes[fd]->file_entry;

    size_t offset = filedes[fd]->offset;
    int32_t real_count = clamp(dir_entry->file_sz - offset, count);
    if(real_count == 0)
        return 0;

    uint16_t read_blk = get_offset_blk(fd, offset);
    if(read_blk == 0) return -1; // nothing can be read

    //read first block
    int32_t real_count_temp = real_count;
    // void *bounce_buffer = calloc(BLOCK_SIZE, 1);
    void *bounce_buffer = malloc(BLOCK_SIZE);
    memset(bounce_buffer, 0, BLOCK_SIZE);
    int buf_idx = 0;
    if(block_read(read_blk + sp->data_blk, bounce_buffer) < 0 ){
        free(bounce_buffer);
        return -1;
    }
    memcpy(buf + buf_idx, bounce_buffer, clamp(real_count_temp, BLOCK_SIZE));

    buf_idx += clamp(real_count_temp, BLOCK_SIZE);
    real_count_temp -= BLOCK_SIZE; // remaining
    read_blk = *(get_fat(read_blk));

    while(real_count_temp > 0 && read_blk != FAT_EOC){
        if(real_count_temp >= BLOCK_SIZE){
            if(block_read(read_blk + sp->data_blk, buf + buf_idx) < 0){
                free(bounce_buffer);
                return -1;
            }
            buf_idx += BLOCK_SIZE;
        }
        else{
            if(block_read(read_blk + sp->data_blk, bounce_buffer) < 0){
                free(bounce_buffer);
                return -1;
            }
            memcpy(buf + buf_idx, bounce_buffer, real_count_temp);
            buf_idx += real_count_temp; // unnecessary acutally
        }
        real_count_temp -= BLOCK_SIZE;
        read_blk = *(get_fat(read_blk));
    }

    free(bounce_buffer);
    if(fs_lseek(fd, real_count + offset) < 0) return -1;

    return real_count;
}

