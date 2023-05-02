#include "vm/mmap.h"
#include "errno.h"
#include "fs/file.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "globals.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/tlb.h"
#include "util/debug.h"

/*
 * This function implements the mmap(2) syscall: Add a mapping to the current
 * process's address space. Supports the following flags: MAP_SHARED,
 * MAP_PRIVATE, MAP_FIXED, and MAP_ANON.
 *
 *  ret - If provided, on success, *ret must point to the start of the mapped area
 *
 * Return 0 on success, or:
 *  - EACCES: 
 *     - A file descriptor refers to a non-regular file.  
 *     - a file mapping was requested, but fd is not open for reading. /// what mode for reading?
 *     - MAP_SHARED was requested and PROT_WRITE is set, but fd is
 *       not open in read/write (O_RDWR) mode.
 *     - PROT_WRITE is set, but the file has FMODE_APPEND specified.
 *  - EBADF: 
 *     - fd is not a valid file descriptor and MAP_ANON was
 *       not set
 *  - EINVAL: done
 *     - addr is not page aligned and MAP_FIXED is specified 
 *     - off is not page aligned 
 *     - len is <= 0 or off < 0 
 *     - flags do not contain MAP_PRIVATE or MAP_SHARED
 *  - ENODEV:
 *     - The underlying filesystem of the specified file does not
 *       support memory mapping or in other words, the file's vnode's mmap
 *       operation doesn't exist
 *  - Propagate errors from vmmap_map()
 * 
 *  See the man page for mmap(2) errors section for more details
 * 
 * Hints:
 *  1) A lot of error checking.
 *  2) Call vmmap_map() to create the mapping.
 *     a) Use VMMAP_DIR_HILO as default, which will make other stencil code in
 *        Weenix happy.
 *  3) Call tlb_flush_range() on the newly-mapped region. This is because the
 *     newly-mapped region could have been used by someone else, and you don't
 *     want to get stale mappings.
 *  4) Don't forget to set ret if it was provided. /// even in error cases?
 * 
 *  If you are mapping less than a page, make sure that you are still allocating 
 *  a full page. 
 */
long do_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off,
             void **ret) /// else statements needed in error checking? /// use == or &?
{
    // file_t *file;
    // vnode_t *vnode;
    // vmarea_t *vma;
    // int err;

    // if ((fd < 0) || (fd >= NFILES)) {
    //     if (!(flags & MAP_ANON)) {
    //         //dbg(DBG_PRINT, "(GRADING3D 1)\n");
    //         return -EBADF;
    //     } /// else case here? invalid fd but MAP_ANON set- should set vnode to NULL? 
    // }
    // if (addr != NULL) {
    //     if (((uint32_t)addr % PAGE_SIZE != 0) && (flags & MAP_FIXED)) {
    //         dbg(DBG_PRINT, "(GRADING3D 1)\n");
    //         return -EINVAL;
    //     }
    // // } else {
    // //     addr = (void *)VMMAP_DIR_LOHI; /// needed?
    // }
    // if (len <= 0 || off < 0) {
    //     return -EINVAL;
    // }
    // if (!(flags & MAP_PRIVATE) && !(flags & MAP_SHARED)) {
    //     return -EINVAL;
    // }

    // if (off % PAGE_SIZE != 0) {
    //     return -EINVAL;
    // }  
    // file = fget(fd);
    // // if (file == NULL) 
    // //     if (flags & MAP_ANON) {
    // //         vnode = NULL;
     
    // if ((file->f_vnode->vn_mode != S_IFREG) || file->f_mode != FMODE_READ){ 
    //     //dbg(DBG_PRINT, "(GRADING3D 1)\n");
    //     return -EACCES;
    // }
    // if ((flags & MAP_SHARED) && (prot & PROT_WRITE)) { /// correct way to check for prot_write?
    //         if (file->f_mode != O_RDWR) {  /// use == or &?
    //             return -EACCES;
    //         }
    //     }
    
    // if (prot & PROT_WRITE) {
    //     if (file->f_vnode->vn_mode == FMODE_APPEND) {
    //         return -EACCES;
    //     }
    // }

    // if (file->f_vnode->vn_ops->mmap == NULL) {
    //     return -ENODEV;
    // }

    // // done with error checking
    // vnode = file->f_vnode;
    // long ret = vmmap_map(curproc->p_vmmap, vnode, ADDR_TO_PN(addr), ADDR_TO_PN(len), prot, flags, ADDR_TO_PN(off), VMMAP_DIR_HILO, &vma);
    // if (vma == NULL) {
    //     return err;
    // }
    // tlb_flush_range((uintptr_t)addr, len);
    // if (ret != NULL) {
    //     *ret = PN_TO_ADDR(vma->vma_start);
    // }
    // return 0;
    

    NOT_YET_IMPLEMENTED("VM: do_mmap");
    return -1;
}

/*
 * This function implements the munmap(2) syscall.
 *
 * Return 0 on success, or:
 *  - EINVAL:
 *     - addr is not aligned on a page boundary
 *     - the region to unmap is out of range of the user address space
 *     - len is 0
 *  - Propagate errors from vmmap_remove()
 * 
 *  See the man page for munmap(2) errors section for more details
 *
 * Hints:
 *  - Similar to do_mmap():
 *  1) Perform error checking.
 *  2) Call vmmap_remove().
 */
long do_munmap(void *addr, size_t len)
{

    if ((uint32_t)addr % PAGE_SIZE != 0) {
        return -EINVAL;
    }
    if (len == 0) {
        return -EINVAL;
    }
    if (addr < (void *)USER_MEM_LOW || addr > (void *)USER_MEM_HIGH) {
        return -EINVAL;
    }
    long err = vmmap_remove(curproc->p_vmmap, ADDR_TO_PN(addr), ADDR_TO_PN(len));
    // if (err != 0) {
    //     return err;
    // }
    return err;

    // NOT_YET_IMPLEMENTED("VM: do_munmap");
    // return -1;
}