#include "errno.h"
#include "fs/fcntl.h"
#include "fs/file.h"
#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "globals.h"
#include "util/debug.h"
#include <fs/vnode.h>

// NOTE: IF DOING MULTI-THREADED PROCS, NEED TO SYNCHRONIZE ACCESS TO FILE
// DESCRIPTORS, AND, MORE GENERALLY SPEAKING, p_files, IN PARTICULAR IN THIS
// FUNCTION AND ITS CALLERS.
/*
 * Go through curproc->p_files and find the first null entry.
 * If one exists, set fd to that index and return 0.
 *
 * Error cases get_empty_fd is responsible for generating:
 *  - EMFILE: no empty file descriptor
 */
long get_empty_fd(int *fd)
{
    for (*fd = 0; *fd < NFILES; (*fd)++)
    {
        if (!curproc->p_files[*fd])
        {
            return 0;
        }
    }
    *fd = -1;
    return -EMFILE;
}

/*
 * Open the file at the provided path with the specified flags.
 *
 * Returns the file descriptor on success, or error cases:
 *  - EINVAL: Invalid oflags
 *  - EISDIR: Trying to open a directory with write access
 *  - ENXIO: Blockdev or chardev vnode does not have an actual underlying device
 *  - ENOMEM: Not enough kernel memory (if fcreate() fails)
 *
 * Hints:
 * 1) Use get_empty_fd() to get an available fd.
 * 2) Use namev_open() with oflags, mode S_IFREG, and devid 0.
 * 3) Check for EISDIR and ENXIO errors.
 * 4) Convert oflags (O_RDONLY, O_WRONLY, O_RDWR, O_APPEND) into corresponding
 *    file access flags (FMODE_READ, FMODE_WRITE, FMODE_APPEND). 
 * 5) Use fcreate() to create and initialize the corresponding file descriptor
 *    with the vnode from 2) and the mode from 4).
 *
 * When checking oflags, you only need to check that the read and write
 * permissions are consistent. However, because O_RDONLY is 0 and O_RDWR is 2,
 * there's no way to tell if both were specified. So, you really only need
 * to check if O_WRONLY and O_RDWR were specified. 
 * 
 * If O_TRUNC specified and the vnode represents a regular file, make sure to call the
 * the vnode's truncate routine (to reduce the size of the file to 0).
 *
 * If a vnode represents a chardev or blockdev, then the appropriate field of
 * the vnode->vn_dev union will point to the device. Otherwise, the union will be NULL.
 */
long do_open(const char *filename, int oflags) 
{
    if ((oflags != O_RDONLY) && (oflags != O_WRONLY) && (oflags != O_RDWR) & (oflags != O_APPEND)){
        return -EINVAL;
    }

    long state = 0;
    int *fd = NULL;
    state = get_empty_fd(fd);
    if (state < 0){
        return state;
    }

    struct vnode *vnode;
    state = namev_open(curproc->p_cwd, filename, oflags, S_IFREG, 0, &vnode);
    if (state < 0){
        return state;
    }

    if (S_ISDIR(vnode->vn_mode) && (oflags & O_WRONLY || oflags & O_RDWR)){
        return -EISDIR;
    }

    if (vnode->vn_mode == S_IFBLK || vnode->vn_mode == S_IFCHR){
        if (vnode->vn_dev.blockdev == NULL){
            return -ENXIO;
        }
    }

    int mode = 0;
    if (oflags & O_RDONLY){
        mode = FMODE_READ;
    } else if (oflags & O_WRONLY){
        mode = FMODE_WRITE;
    } else if (oflags & O_RDWR){
        mode = FMODE_READ | FMODE_WRITE;
    } else if (oflags & O_APPEND){
        mode = FMODE_APPEND;
    }

    if (oflags & O_TRUNC && S_ISREG(vnode->vn_mode)){
        vnode->vn_ops->truncate_file(vnode);
    }

    file_t* file = NULL;
    file = fcreate(mode, vnode, *fd);
    if (file == NULL){ /// correct?
        return -ENOMEM;
    }

    vput(&vnode);

    return *fd;
    
    //NOT_YET_IMPLEMENTED("VFS: do_open");
    //return -1;
}
