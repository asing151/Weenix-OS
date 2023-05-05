#include "fs/s5fs/s5fs_subr.h"
#include "drivers/blockdev.h"
#include "errno.h"
#include "fs/s5fs/s5fs.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "kernel.h"
#include "mm/pframe.h"
#include "proc/kmutex.h"
#include "util/debug.h"
#include "util/string.h"
#include <fs/s5fs/s5fs.h>

static void s5_free_block(s5fs_t *s5fs, blocknum_t block);

static long s5_alloc_block(s5fs_t *s5fs);

static inline void s5_lock_super(s5fs_t *s5fs)
{
    kmutex_lock(&s5fs->s5f_mutex);
}

static inline void s5_unlock_super(s5fs_t *s5fs)
{
    kmutex_unlock(&s5fs->s5f_mutex);
}

/* Helper function to obtain inode info from disk given an inode number.
 *
 *  s5fs     - The file system (it will usually be obvious what to pass for this
 *             parameter)
 *  ino      - Inode number to fetch
 *  forwrite - Set if you intend to write any fields in the s5_inode_t, clear
 *             if you only intend to read
 *  pfp      - Return parameter for a page frame that will contain the disk
 *             block of the desired inode
 *  inodep   - Return parameter for the s5_inode_t corresponding to the desired
 *             inode
 */
static inline void s5_get_inode(s5fs_t *s5fs, ino_t ino, long forwrite,
                                pframe_t **pfp, s5_inode_t **inodep)
{
    s5_get_disk_block(s5fs, S5_INODE_BLOCK(ino), forwrite, pfp);
    *inodep = (s5_inode_t *)(*pfp)->pf_addr + S5_INODE_OFFSET(ino);
    KASSERT((*inodep)->s5_number == ino);
}

/* Release an inode by releasing the page frame of the disk block containing the
 * inode. See comments above s5_release_disk_block to see why we don't write
 * anything back yet.
 *
 *  pfp    - The page frame containing the inode
 *  inodep - The inode to be released
 *
 * On return, pfp and inodep both point to NULL.
 */
static inline void s5_release_inode(pframe_t **pfp, s5_inode_t **inodep)
{
    KASSERT((s5_inode_t *)(*pfp)->pf_addr +
                S5_INODE_OFFSET((*inodep)->s5_number) ==
            *inodep);
    *inodep = NULL;
    s5_release_disk_block(pfp);
}

/* Helper function to obtain a specific block of a file.
 *
 * sn       - The s5_node representing the file in question
 * blocknum - The offset of the desired block relative to the beginning of the
 *            file, i.e. index 8000 is block 1 of the file, even though it may
 *            not be block 1 of the disk
 * forwrite - Set if you intend to write to the block, clear if you only intend
 *            to read
 * pfp      - Return parameter for a page frame containing the block data
 */
static inline long s5_get_file_block(s5_node_t *sn, size_t blocknum,
                                     long forwrite, pframe_t **pfp)
{
    return sn->vnode.vn_mobj.mo_ops.get_pframe(&sn->vnode.vn_mobj, blocknum,
                                               forwrite, pfp);
}

/* Release the page frame associated with a file block. See comments above
 * s5_release_disk_block to see why we don't write anything back yet.
 *
 * On return, pfp points to NULL.
 */
static inline void s5_release_file_block(pframe_t **pfp)
{
    pframe_release(pfp);
}

/* Given a file and a file block number, return the disk block number of the
 * desired file block.
 *
 *  sn            - The s5_node representing the file
 *  file_blocknum - The offset of the desired block relative to the beginning of
 *                  the file
 *  alloc         - If set, allocate the block / indirect block as necessary
 *                  If clear, don't allocate sparse blocks
 *
 * Return a disk block number on success, or:
 *  - 0: The block is sparse, and alloc is clear, OR
 *       The indirect block would contain the block, but the indirect block is
 *       sparse, and alloc is clear
 *  - EINVAL: The specified block number is greater than or equal to 
 *            S5_MAX_FILE_BLOCKS
 *  - Propagate errors from s5_alloc_block.
 *
 * Hints:
 *  - Use the file inode's s5_direct_blocks and s5_indirect_block to perform the
 *    translation.
 *  - Use s5_alloc_block to allocate blocks.
 *  - Be sure to mark the inode as dirty when appropriate, i.e. when you are
 *    making changes to the actual s5_inode_t struct. Hint: Does allocating a
 *    direct block dirty the inode? What about allocating the indirect block?
 *    Finally, what about allocating a block pointed to by the indirect block?
 *  - Cases to consider:
 *    1) file_blocknum < S_NDIRECT_BLOCKS
 *    2) Indirect block is not allocated but alloc is set. Be careful not to
 *       leak a block in an error case!
 *    3) Indirect block is allocated. The desired block may be sparse, and you
 *       may have to allocate it.
 *    4) The indirect block has not been allocated and alloc is clear.
 */
long s5_file_block_to_disk_block(s5_node_t *sn, size_t file_blocknum,
                                 int alloc)
{

    // if (file_blocknum >= S5_MAX_FILE_BLOCKS) {
    //     return -EINVAL;
    // }

    // // get this s5fs_t from sn
    // s5fs_t *s5fs = VNODE_TO_S5FS(&sn->vnode);

    // if (file_blocknum < S5_NDIRECT_BLOCKS) {
    //     // chekcing if 
    //     if (sn->inode->s5_direct_blocks[file_blocknum] == 0) { /// compare to NULL instead of 0?
    //         if (alloc) {
    //             long block = s5_alloc_block(s5fs);
    //             if (block < 0) {
    //                 return block;
    //             }
    //             sn->inode->s5_direct_blocks[file_blocknum] = block;
    //             sn->dirtied_inode = 1;
    //             return block;
    //         } else {
    //             return 0;
    //         }
    //     }
    //     return sn->inode->s5_direct_blocks[file_blocknum];
    // }

    // if (sn->inode->s5_indirect_block == 0) {
    //     if (alloc) {
    //         long indirect_block = s5_alloc_block(s5fs);
    //         if (block < 0) {
    //             return block;
    //         }
    //         sn->inode->s5_indirect_block = indirect_block;
    //         sn->dirtied_inode = 1;
    //         long actual = s5_alloc_block(s5fs);
            
    //         // declare a pframe
    //         s5_get_disk_block(s5fs, indirect_block, 1, said_pframe->discblocknum); /// write actual to pframe

    //     } else {
    //         return 0;
    //     }
    // } else {
    //      pframe_t *pframe;
    // s5_get_disk_block(sn->s5_fs, sn->inode->s5_indirect_block,
    //                             0, &pframe);
    // }

    // uint32_t *indirect_block = (uint32_t *)pframe->pf_addr;
    // long block = indirect_block[file_blocknum - S5_NDIRECT_BLOCKS];

    // if (block == 0) {
    //     s5_release_disk_block(&pframe);

    //     if (alloc) {
    //         block = s5_alloc_block(sn->s5_fs);
    //         if (block < 0) {
    //             // s5_release_disk_block(&pframe);
    //             return block;
    //         }
    //          s5_get_disk_block(sn->s5_fs, sn->inode->s5_indirect_block,
    //                             1, &pframe);
    //         *indirect_block = (uint32_t *)pframe->pf_addr;
    //         indirect_block[file_blocknum - S5_NDIRECT_BLOCKS] = block;
    //         sn->dirtied_inode = 1;
    //         s5_release_disk_block(&pframe);
    //     } 
    // } else {
    //     s5_release_disk_block(&pframe);
    // }
    // // s5_release_disk_block(&pframe);
    // return block;
    return 0;

    NOT_YET_IMPLEMENTED("S5FS: s5_file_block_to_disk_block");

}

/* Read from a file.
 *
 *  sn  - The s5_node representing the file to read from
 *  pos - The position to start reading from
 *  buf - The buffer to read into
 *  len - The number of bytes to read
 *
 * Return the number of bytes read, or:
 *  - Propagate errors from s5_get_file_block (do not return a partial
 *    read). As in, if s5_get_file_block returns an error, 
 *    the call to s5_read_file should fail.
 *
 * Hints:
 *  - Do not directly call s5_file_block_to_disk_block. To obtain pframes with
 *    the desired blocks, use s5_get_file_block and s5_release_file_block.
 *  - Be sure to handle all edge cases regarding pos and len relative to the
 *    length of the actual file. (If pos is greater than or equal to the length
 *    of the file, then s5_read_file should return 0). 
 */
ssize_t s5_read_file(s5_node_t *sn, size_t pos, char *buf, size_t len)
{
    // size_t bytes_read = 0;
    // size_t bytes_to_read = len;
    // size_t bytes_left = sn->inode->s5_size - pos;
    // if (pos >= sn->inode->s5_size) {
    //     return 0;
    // }
    // if (len > bytes_left) {
    //     bytes_to_read = bytes_left;
    // }
    // size_t file_blocknum = S5_DATA_BLOCK(pos);
    // size_t offset = S5_DATA_OFFSET(pos);
    // size_t bytes_to_read_from_block = S5_BLOCK_SIZE - offset;
    // if (bytes_to_read_from_block > bytes_to_read) {
    //     bytes_to_read_from_block = bytes_to_read;
    // } 
    
    // while (bytes_read < bytes_to_read) {
    //     pframe_t *pframe;
    //     int ret = s5_get_file_block(sn, file_blocknum, &pframe); 
    //     if (ret < 0) {
    //         return ret;
    //     }

    //     memcpy(buf + bytes_read, pframe->pf_addr + offset, bytes_to_read_from_block);
    //     //s5_release_file_block
    //     s5_release_file_block(sn, file_blocknum);
    //     bytes_read += bytes_to_read_from_block;
    //     //file_blocknum += 1;
    //     //offset = 0;
    //     // bytes_to_read_from_block = S5_BLOCK_SIZE;
    //     // if (bytes_to_read_from_block > bytes_to_read - bytes_read) {
    //     //     bytes_to_read_from_block = bytes_to_read - bytes_read;
    //     }
    
    // return bytes_read;
    NOT_YET_IMPLEMENTED("S5FS: s5_read_file");
    return -1;
}

/* Write to a file.
 *
 *  sn  - The s5_node representing the file to write to
 *  pos - The position to start writing to
 *  buf - The buffer to write from
 *  len - The number of bytes to write
 *
 * Return the number of bytes written, or:
 *  - EFBIG: pos was beyond S5_MAX_FILE_SIZE
 *  - Propagate errors from s5_get_file_block (that is, do not return a partial
 *    write)
 *
 * Hints:
 *  - You should return -EFBIG only if the provided pos was invalid. Otherwise,
 *    it is okay to make a partial write up to the maximum file size.
 *  - Use s5_get_file_block and s5_release_file_block to obtain pframes with
 *    the desired blocks.
 *  - Because s5_get_file_block calls s5fs_get_pframe, which checks the length
 *    of the vnode, you may have to update the vnode's length before you call
 *    s5_get_file_block. In this case, you should also update the inode's
 *    s5_size and mark the inode dirty.
 *  - If, midway through writing, you run into an error with s5_get_file_block,
 *    it is okay to merely undo your most recent changes while leaving behind
 *    writes you've already made to other blocks, before returning the error.
 *    That is, it is okay to make a partial write that the caller does not know
 *    about, as long as the file's length is consistent with what you've
 *    actually written so far.
 *  - You should maintain the vn_len of the vnode and the s5_un.s5_size field of the 
 *    inode to be the same. 
 */
ssize_t s5_write_file(s5_node_t *sn, size_t pos, const char *buf, size_t len)
{
    // size_t bytes_written = 0;
    // size_t bytes_to_write = len;
    // size_t bytes_left = S5_MAX_FILE_SIZE - pos;
    // if (pos > S5_MAX_FILE_SIZE) {
    //     return -EFBIG;
    // }
    // if (len > bytes_left) {
    //     bytes_to_write = bytes_left;
    // }
    // size_t file_blocknum = S5_DATA_BLOCK(pos);
    // size_t offset = S5_DATA_OFFSET(pos);
    // size_t bytes_to_write_to_block = S5_BLOCK_SIZE - offset;
    // if (bytes_to_write_to_block > bytes_to_write) {
    //     bytes_to_write_to_block = bytes_to_write;
    // }

    // if (pos + bytes_written > sn->inode->s5_size) { /// correct?
    //     sn->inode->s5_size = pos + bytes_to_write;
    //     sn->vn.vn_len = sn->inode->s5_size;
    //     s5->dirtied_inode = 1;
    // } 

    // // in this while loop, we are writing to the file block by block
    // while (bytes_written < bytes_to_write) {
    //     pframe_t *pframe;
    //     int ret = s5_get_file_block(sn, file_blocknum, &pframe);
    //     if (ret < 0) {
    //         return ret;
    //     }
    //     memcpy(pframe->pf_addr + offset, buf, bytes_to_write_to_block);
    //     s5_release_file_block(&pframe);
    //     bytes_written += bytes_to_write_to_block;
    //     // file_blocknum++;
    //     // offset = 0;
    //     // bytes_to_write_to_block = S5_BLOCK_SIZE;
    //     // if (bytes_to_write_to_block > bytes_to_write - bytes_written) {
    //     //     bytes_to_write_to_block = bytes_to_write - bytes_written;
    //     // }
    // }

    
    // return bytes_written;
    NOT_YET_IMPLEMENTED("S5FS: s5_write_file");
    return -1;
}

/* Allocate one block from the filesystem.
 *
 * Return the block number of the newly allocated block, or:
 *  - ENOSPC: There are no more free blocks
 *
 * Hints:
 *  - Protect access to the super block using s5_lock_super and s5_unlock super.
 *  - Recall that the free block list is a linked list of blocks containing disk
 *    block numbers of free blocks. Each node contains S5_NBLKS_PER_FNODE block
 *    numbers, where the last entry is a pointer to the next node in the linked
 *    list, or -1 if there are no more free blocks remaining. The super block's
 *    s5s_free_blocks is the first node of this linked list.
 *  - The super block's s5s_nfree member is the number of blocks that are free
 *    within s5s_free_blocks. You could use it as an index into the
 *    s5s_free_blocks array. Be sure to update the field appropriately.
 *  - When s5s_free_blocks runs out (i.e. s5s_nfree == 0), refill it by
 *    collapsing the next node of the free list into the super block. Exactly
 *    when you do this is up to you.
 *  - You should initialize the block's contents to 0. Specifically, 
 *    when you use s5_alloc_block to allocate an indirect block,
 *    as your implementation of s5_file_block_to_disk_block probably expects
 *    sparse blocks to be represented by a 0.
 *  - You may find it helpful to take a look at the implementation of
 *    s5_free_block below.
 *  - You may assume/assert that any pframe calls succeed.
 */
static long s5_alloc_block(s5fs_t *s5fs)
{
    // s5_lock_super(s5fs);
    // s5_super_t *s = &s5fs->s5f_super;
    // pframe_t *pframe;
    // if (s->s5s_nfree == 0) {
    //     blocknum_t blockno = s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1];
        
    //     int pf_addr = s5_get_disk_block(s5fs, blockno, 1, &pframe); /// right args?
    //     if (pf_addr < 0) {
    //         return ret;
    //     }

    //     //s5_fnode_t *fnode = (s5_fnode_t *)pframe->pf_addr;

    //     memcpy(s->s5s_free_blocks, pframe->pf_addr, sizeof(s->s5s_free_blocks)); /// memcopy into s5 freeblock the pfaddr which I get from get disk block
    //     s->s5s_nfree = S5_NBLKS_PER_FNODE - 1;
    // } else {
    //     s->s5s_nfree--;
    //     blocknum_t blockno = s->s5s_free_blocks[s->s5s_nfree - 1];
    //     //s5_unlock_super(s5fs); /// not needed in if statement?
    //     int ret = s5_get_disk_block(s5fs, blockno, 1, &pframe); // right args?
    //     if (ret < 0) {
    //     return ret;
    //     }
    //     //memset()//// on given pframe
    //     ret = memset(pframe->pf_addr, 0, PAGE_SIZE);
    //     if (ret < 0) {
    //         return ret;
    //     }
    // }

    // s5_release_disk_block(&pframe);
    // memset(pframe->pf_addr, 0, PAGE_SIZE);
    // s5_unlock_super(s5fs);
    // return blockno;
    
    NOT_YET_IMPLEMENTED("S5FS: s5_alloc_block");
    return -1;
}

/*
 * The exact opposite of s5_alloc_block: add blockno to the free list of the
 * filesystem. This should never fail. You may assert that any pframe calls
 * succeed.
 *
 * Don't forget to protect access to the super block, update s5s_nfree, and
 * expand the linked list correctly if the super block can no longer hold any
 * more free blocks in its s5s_free_blocks array according to s5s_nfree.
 */
static void s5_free_block(s5fs_t *s5fs, blocknum_t blockno)
{
    s5_lock_super(s5fs);
    s5_super_t *s = &s5fs->s5f_super;
    dbg(DBG_S5FS, "freeing disk block %d\n", blockno);
    KASSERT(blockno);
    KASSERT(s->s5s_nfree < S5_NBLKS_PER_FNODE);

    pframe_t *pf;
    s5_get_disk_block(s5fs, blockno, 1, &pf);

    if (s->s5s_nfree == S5_NBLKS_PER_FNODE - 1)
    {
        memcpy(pf->pf_addr, s->s5s_free_blocks, sizeof(s->s5s_free_blocks));

        s->s5s_nfree = 0;
        s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1] = blockno;
    }
    else
    {
        s->s5s_free_blocks[s->s5s_nfree++] = blockno;
        pf->pf_dirty = 0;
    }
    s5_release_disk_block(&pf);
    s5_unlock_super(s5fs);
}

/*
 * Allocate one inode from the filesystem. You will need to use the super block
 * s5s_free_inode member. You must initialize the on-disk contents of the
 * allocated inode according to the arguments type and devid.
 *
 * Recall that the free inode list is a linked list. Each free inode contains a
 * link to the next free inode. The super block s5s_free_inode must always point
 * to the next free inode, or contain -1 to indicate no more inodes are
 * available.
 *
 * Don't forget to protect access to the super block and update s5s_free_inode.
 *
 * You should use s5_get_inode and s5_release_inode.
 *
 * On success, return the newly allocated inode number.
 * On failure, return -ENOSPC.
 */
long s5_alloc_inode(s5fs_t *s5fs, uint16_t type, devid_t devid)
{
    KASSERT((S5_TYPE_DATA == type) || (S5_TYPE_DIR == type) ||
            (S5_TYPE_CHR == type) || (S5_TYPE_BLK == type));

    s5_lock_super(s5fs);
    uint32_t new_ino = s5fs->s5f_super.s5s_free_inode;
    if (new_ino == (uint32_t)-1)
    {
        s5_unlock_super(s5fs);
        return -ENOSPC;
    }

    pframe_t *pf;
    s5_inode_t *inode;
    s5_get_inode(s5fs, new_ino, 1, &pf, &inode);

    s5fs->s5f_super.s5s_free_inode = inode->s5_un.s5_next_free;
    KASSERT(inode->s5_un.s5_next_free != inode->s5_number);

    inode->s5_un.s5_size = 0;
    inode->s5_type = type;
    inode->s5_linkcount = 0;
    memset(inode->s5_direct_blocks, 0, sizeof(inode->s5_direct_blocks));
    inode->s5_indirect_block =
        (S5_TYPE_CHR == type || S5_TYPE_BLK == type) ? devid : 0;

    s5_release_inode(&pf, &inode);
    s5_unlock_super(s5fs);

    dbg(DBG_S5FS, "allocated inode %d\n", new_ino);
    return new_ino;
}

/*
 * Free the inode by:
 *  1) adding the inode to the free inode linked list (opposite of
 * s5_alloc_inode), and 2) freeing all blocks being used by the inode.
 *
 * The suggested order of operations to avoid deadlock, is:
 *  1) lock the super block
 *  2) get the inode to be freed
 *  3) update the free inode linked list
 *  4) copy the blocks to be freed from the inode onto the stack
 *  5) release the inode
 *  6) unlock the super block
 *  7) free all direct blocks
 *  8) get the indirect block
 *  9) copy the indirect block array onto the stack
 *  10) release the indirect block
 *  11) free the indirect blocks
 *  12) free the indirect block itself
 */
void s5_free_inode(s5fs_t *s5fs, ino_t ino)
{
    pframe_t *pf;
    s5_inode_t *inode;
    s5_lock_super(s5fs);
    s5_get_inode(s5fs, ino, 1, &pf, &inode);

    uint32_t direct_blocks_to_free[S5_NDIRECT_BLOCKS];
    uint32_t indirect_block_to_free;
    if (inode->s5_type == S5_TYPE_DATA || inode->s5_type == S5_TYPE_DIR)
    {
        indirect_block_to_free = inode->s5_indirect_block;
        memcpy(direct_blocks_to_free, inode->s5_direct_blocks,
               sizeof(direct_blocks_to_free));
    }
    else
    {
        KASSERT(inode->s5_type == S5_TYPE_BLK || inode->s5_type == S5_TYPE_CHR);
        indirect_block_to_free = 0;
        memset(direct_blocks_to_free, 0, sizeof(direct_blocks_to_free));
    }

    inode->s5_un.s5_next_free = s5fs->s5f_super.s5s_free_inode;
    inode->s5_type = S5_TYPE_FREE;
    s5fs->s5f_super.s5s_free_inode = inode->s5_number;

    s5_release_inode(&pf, &inode);
    s5_unlock_super(s5fs);

    for (unsigned i = 0; i < S5_NDIRECT_BLOCKS; i++)
    {
        if (direct_blocks_to_free[i])
        {
            s5_free_block(s5fs, direct_blocks_to_free[i]);
        }
    }
    if (indirect_block_to_free)
    {
        uint32_t indirect_blocks_to_free[S5_NIDIRECT_BLOCKS];

        s5_get_disk_block(s5fs, indirect_block_to_free, 0, &pf);
        KASSERT(S5_BLOCK_SIZE == PAGE_SIZE);
        memcpy(indirect_blocks_to_free, pf->pf_addr, S5_BLOCK_SIZE);
        s5_release_disk_block(&pf);

        for (unsigned i = 0; i < S5_NIDIRECT_BLOCKS; i++)
        {
            if (indirect_blocks_to_free[i])
            {
                s5_free_block(s5fs, indirect_blocks_to_free[i]);
            }
        }
        s5_free_block(s5fs, indirect_block_to_free);
    }
    dbg(DBG_S5FS, "freed inode %d\n", ino);
}

/* Return the inode number corresponding to the directory entry specified by
 * name and namelen within a given directory.
 *
 *  sn      - The directory to search in
 *  name    - The name to search for
 *  namelen - Length of name
 *  filepos - If non-NULL, use filepos to return the starting position of the
 *            directory entry
 *
 * Return the desired inode number, or:
 *  - ENOENT: Could not find a directory entry with the specified name
 *
 * Hints:
 *  - Use s5_read_file in increments of sizeof(s5_dirent_t) to read successive
 *    directory entries and compare them against name and namelen.
 *  - To avoid reading beyond the end of the directory, check if the return 
 *    value of s5_read_file is 0
 *  - You could optimize this function by using s5_get_file_block (rather than
 *    s5_read_file) to ensure you do not read beyond the length of the file,
 *    but doing so is optional.
 */
long s5_find_dirent(s5_node_t *sn, const char *name, size_t namelen,
                    size_t *filepos)
{
    // KASSERT(S_ISDIR(sn->vnode.vn_mode) );
    // KASSERT(S5_BLOCK_SIZE == PAGE_SIZE );
    /// size, p
    // start writing 

    /// name_match
    //size_t offset = 0;
    // if (filepos)
    // {
    //     offset = *filepos;
    // }


    // s5_dirent_t dirent;
    // size_t offset = 0
    // while (offset < len(vnode from sn))
    // {
    //     // if s5_read_file(sn, count, dirent, sizeof(s5_dirent_t)) works:
    //     // check if dirent inode is allocated (!= -1)

    //     int ret = s5_read_file(sn, offset, dirent, sizeof(s5_dirent_t));
    //     if (ret != 0) /// this if check right?
    //     {
    //         return ret;
    //     } /// need an else here?

    //     if (strncmp(dirent.s5d_name, name, namelen) == 0)
    //     {
    //         if (filepos)
    //         {
    //             *filepos = offset;
    //         }
    //         return dirent.s5d_ino;
    //     }
    //     offset += sizeof(s5_dirent_t);
    // }

    // return -ENOENT;
    NOT_YET_IMPLEMENTED("S5FS: s5_find_dirent");
    return -1;
}

/* Remove the directory entry specified by name and namelen from the directory
 * sn.
 *
 *  child - The found directory entry must correspond to the caller-provided
 *          child
 *
 * No return value. This function should never fail. You should assert that
 * anything which could be incorrect is correct, and any function calls which
 * could fail succeed.
 *
 * Hints:
 *  - Assert that the directory exists.
 *  - Assert that the found directory entry corresponds to child.
 *  - Ensure that the remaining directory entries in the file are contiguous. To
 *    do this, you should:
 *    - Overwrite the removed entry with the last directory entry.
 *    - Truncate the length of the directory by sizeof(s5_dirent_t).
 *  - Make sure you are only using s5_dirent_t, and not dirent_t structs.
 *  - Decrement the child's linkcount, because you have removed the directory's
 *    link to the child.
 *  - Mark the inodes as dirtied.
 *  - Use s5_find_dirent to find the position of the entry being removed. 
 */
void s5_remove_dirent(s5_node_t *sn, const char *name, size_t namelen,
                      s5_node_t *child)
{
//     vnode_t *dir = &sn->vnode;
//     s5_inode_t *inode = &sn->inode;

//     KASSERT(S_ISDIR(dir->vn_mode));
    
//     // kassert to check that found directory entry corresponds to child
//    /// how to check if it is actually the child? - compare outout of find with child->inode->s5 number
//     size_t filepos;
//     long ino = s5_find_dirent(sn, name, namelen, &filepos);
//     KASSERT(ino != -1);//// ERR PROpagate inst
//     KASSERT(child->inode.s5_ino == ino);

//     // next steps:
//     // initilize a char buf of length sizeof(s5_dirent_t)
//     // read the last dirent into the buf
//     // write the buf to the filepos
//     // truncate the file by sizeof(s5_dirent_t)
//     // decrement the child's linkcount
//     // mark the inodes as dirtied

//     char buf[sizeof(s5_dirent_t)];
//     size_t last_dirent_pos = inode->s5_size - sizeof(s5_dirent_t);
//     s5_read_file(sn, buf, last_dirent_pos, sizeof(s5_dirent_t));

//     //// args are switched probably
//     s5_write_file(sn, buf, filepos, sizeof(s5_dirent_t));
    
//     // truncate by adjusting vn len
//     dir->vn_len -= sizeof(s5_dirent_t); /// inode len n vn len decr
//     child->inode.s5_linkcount--;
//     sn->dirtied_inode = 1;
//     child->dirtied_inode = 1;
//     sn->//// update size, decr bu dirent

    // do all relevant cleanup now
    

    // do find dirent again to get filepos, read the file at the end and overwrite the dirent to remove with the last dirent
    // shorten everything, use a intermediary buf, findirent, read file, write file

    NOT_YET_IMPLEMENTED("S5FS: s5_remove_dirent");
}

/* Replace a directory entry.
 *
 *  sn      - The directory to search within
 *  name    - The name of the old directory entry
 *  namelen - Length of the old directory entry name
 *  old     - The s5_node corresponding to the old directory entry
 *  new     - The s5_node corresponding to the new directory entry
 *
 * No return value. Similar to s5_remove_dirent, this function should never
 * fail. You should assert that everything behaves correctly.
 *
 * Hints:
 *  - Assert that the directory exists, that the directory entry exists, and
 *    that it corresponds to the old s5_node.
 *  - When forming the new directory entry, use the same name and namelen from
 *    before, but use the inode number from the new s5_node.
 *  - Update linkcounts and dirty inodes appropriately.
 *
 * s5_replace_dirent is NOT necessary to implement. It's only useful if 
 * you're planning on implementing the renaming of directories (which you shouldn't 
 * attempt until after the rest of S5FS is done).
 */
void s5_replace_dirent(s5_node_t *sn, const char *name, size_t namelen,
                       s5_node_t *old, s5_node_t *new)
{
    // vnode_t *dir = &sn->vnode;
    // s5_inode_t *inode = &sn->inode;

    // KASSERT(S_ISDIR(dir->vn_mode));
    // // kassert to check that found directory entry corresponds to child
    // /// how to check if it is actually the child?
    // size_t filepos;
    // long ino = s5_find_dirent(sn, name, namelen, &filepos);
    // KASSERT(ino != -1);
    // KASSERT(old->inode.s5_ino == ino);



    // // size_t filepos;
    // // long ino = s5_find_dirent(sn, name, namelen, &filepos);
    // // KASSERT(ino != -1);

    NOT_YET_IMPLEMENTED("RENAMEDIR: s5_replace_dirent()");
}

/* Create a directory entry.
 *
 *  dir     - The directory within which to create a new entry
 *  name    - The name of the new entry
 *  namelen - Length of the new entry name
 *  child   - The s5_node holding the inode which the new entry should represent
 *
 * Return 0 on success, or:
 *  - EEXIST: The directory entry already exists
 *  - Propagate errors from s5_write_file
 
 * Hints:
 *  - Update linkcounts and mark inodes dirty appropriately.
 *  - You may wish to assert at the end of s5_link that the directory entry
 *    exists and that its inode is, as expected, the inode of child.
 */
long s5_link(s5_node_t *dir, const char *name, size_t namelen,
             s5_node_t *child)
{
    // KASSERT(kmutex_owns_mutex(&dir->vnode.vn_mobj.mo_mutex));

    // // check if the directory entry already exists
    // size_t filepos;
    // long ino = s5_find_dirent(dir, name, namelen, &filepos);
    // if (ino != -1)
    // {
    //     return -EEXIST;
    // }

    // // create a new directory entry
    // s5_dirent_t dirent;
    // dirent.s5d_ino = child->inode.s5i_no;
    // dirent.s5d_reclen = sizeof(s5_dirent_t);
    // dirent.s5d_namelen = namelen;
    // dirent.s5d_name[namelen] = '\0';
    // memcpy(dirent.s5d_name, name, namelen);
    
    // // write the new directory entry to the file
    // int err = s5_write_file(dir, dir->vnode.vn_len, dirent, sizeof(s5_dirent_t)); //// casting for dirent + calls to write file througout this doc
    // if (err != 0)
    // {
    //     return err;
    // }

    // // update linkcounts and mark inodes dirty
    // child->inode.s5i_nlink++;

    // child->diretied_inode = 1;


    // /// needed? call find dir at the end again to verify that the directory entry exists and that its inode is, as expected, the inode of child
    // size_t filepos2;
    // long ino2 = s5_find_dirent(dir, name, namelen, &filepos2);
    // KASSERT(ino2 != -1);
    // KASSERT(child->inode.s5i_no == ino2);


    NOT_YET_IMPLEMENTED("S5FS: s5_link");
    return -1;
}

/* Return the number of file blocks allocated for sn. This means any
 * file blocks that are not sparse, direct or indirect. If the indirect
 * block itself is allocated, that must also count. This function should not
 * fail.
 *
 * Hint:
 *  - You may wish to assert that the special character / block files do not
 *    have any blocks allocated to them. Remember, the s5_indirect_block for
 *    these special files is actually the device id.
 */
long s5_inode_blocks(s5_node_t *sn)
{
    // assert that the special character / block files do not
//  *    have any blocks allocated to them. Remember, the s5_indirect_block for
//  *    these special files is actually the device id.

    //KASSERT(sn->inode.s5i_indirect_block != 0);

    // // count the number of blocks allocated for sn
    // int count = 0;
    // for (unsigned i = 0; i < S5_NDIRECT_BLOCKS; i++)
    // {
    //     if (sn->inode.s5_direct_blocks[i] != 0)
    //     {
    //         count++;
    //     }
    // }

    // if (sn->inode.s5_indirect_block != 0)
    // {
    //     count++;
    //     /// count indirent blocks also 
    //     s5_indirect_block_t* indirect_block = (s5_indirect_block_t*)sn->inode.s5_indirect_block;
    //     for (unsigned i = 0; i < S5_NINDIRECT_BLOCKS; i++)
    //     {
    //         if (indirect_block->s5_indirect_blocks[i] != 0)
    //         {
    //             count++;
    //         }
    //     }
    // }

    // return count;

    NOT_YET_IMPLEMENTED("S5FS: s5_inode_blocks");
    return -1;
}

/**
 * Given a s5_node_t, frees the associated direct blocks and 
 * the indirect blocks if they exist. 
 * 
 * Should only be called from the truncate_file routine. 
 */
void s5_remove_blocks(s5_node_t *sn)
{
    // Free the blocks used by the node
    // First, free the the direct blocks
    s5fs_t* s5fs = VNODE_TO_S5FS(&sn->vnode);
    s5_inode_t* s5_inode = &sn->inode; 
    for (unsigned i = 0; i < S5_NDIRECT_BLOCKS; i++) 
    {
        if (s5_inode->s5_direct_blocks[i])
        {
            s5_free_block(s5fs, s5_inode->s5_direct_blocks[i]);
        }
    }

    memset(s5_inode->s5_direct_blocks, 0, sizeof(s5_inode->s5_direct_blocks));

    // Get the indirect blocks and free them, if they exist
    if (s5_inode->s5_indirect_block)
    {
        pframe_t *pf;
        s5_get_disk_block(s5fs, s5_inode->s5_indirect_block, 0, &pf);
        uint32_t *blocknum_ptr = pf->pf_addr;

        for (unsigned i = 0; i < S5_NIDIRECT_BLOCKS; i++)
        {
            if (blocknum_ptr[i])
            {
                s5_free_block(s5fs, blocknum_ptr[i]);
            }
        }

        s5_release_disk_block(&pf);
        // Free the indirect block itself
        s5_free_block(s5fs, s5_inode->s5_indirect_block);
        s5_inode->s5_indirect_block = 0;
    }
}
