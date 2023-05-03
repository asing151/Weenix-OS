#include "fs/vfs_syscall.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "fs/file.h"
#include "fs/lseek.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "globals.h"
#include "kernel.h"
#include "util/debug.h"
#include "util/string.h"
#include <limits.h>

/*
 * Read len bytes into buf from the fd's file using the file's vnode operation
 * read.
 *
 * Return the number of bytes read on success, or:
 *  - EBADF: f d is invalid or is not open for reading
 *  - EISDIR: fd refers to a directory
 *  - Propagate errors from the vnode operation read
 *
 * Hints:
 *  - Be sure to update the file's position appropriately.
 *  - Lock/unlock the file's vnode when calling its read operation.
 */
ssize_t do_read(int fd, void *buf, size_t len)
{
    file_t *file = fget(fd);
    if (file == NULL ) {
        return -EBADF;
    }
    if (file->f_mode & FMODE_READ) {
        if (S_ISDIR(file->f_vnode->vn_mode)) { // can be outside this if
            fput(&file); 
            return -EISDIR;
        }
        vlock(file->f_vnode);
        int ret = file->f_vnode->vn_ops->read(file->f_vnode, file->f_pos, buf, len);
        if (ret != 0) {
            vunlock(file->f_vnode);
            fput(&file);
            return ret;
        }

        file->f_pos += ret;
        vunlock(file->f_vnode);
        fput(&file);

        return ret;
    } else {
        fput(&file);
        return -EBADF;
    }
    //NOT_YET_IMPLEMENTED("VFS: do_read");
}

/*
 * Write len bytes from buf into the fd's file using the file's vnode operation
 * write.
 *
 * Return the number of bytes written on success, or:
 *  - EBADF: fd is invalid or is not open for writing
 *  - Propagate errors from the vnode operation read
 *
 * Hints:
 *  - Check out `man 2 write` for details about how to handle the FMODE_APPEND
 *    flag.
 *  - Be sure to update the file's position appropriately.
 *  - Lock/unlock the file's vnode when calling its write operation.
 */
ssize_t do_write(int fd, const void *buf, size_t len)
{
    file_t *file = fget(fd);
    if (file == NULL ) {
        return -EBADF;
    }
    if (file->f_mode & FMODE_WRITE) {
        vlock(file->f_vnode);
        if (file->f_mode & FMODE_APPEND) {
            file->f_pos = file->f_vnode->vn_len;
        }
        int ret = file->f_vnode->vn_ops->write(file->f_vnode, file->f_pos, buf, len);
        if (ret != 0) {
            vunlock(file->f_vnode);
            fput(&file);
            return ret;
        }

        file->f_pos += ret;
        vunlock(file->f_vnode);
        fput(&file);

        return ret;
    } else {
        fput(&file);
        return -EBADF;
    }
    //NOT_YET_IMPLEMENTED("VFS: do_write");
    
    //return -1;
}

/*
 * Close the file descriptor fd.
 *
 * Return 0 on success, or:
 *  - EBADF: fd is invalid or not open
 * 
 * Hints: 
 * Check `proc.h` to see if there are any helpful fields in the 
 * proc_t struct for checking if the file associated with the fd is open. 
 * Consider what happens when we open a file and what counts as closing it
 */
long do_close(int fd)
{
    if (fd < 0 || fd >= NFILES) {
        return -EBADF;
    } else { 
        if (curproc->p_files[fd] == NULL) {
        return -EBADF;
        }
        fput(&curproc->p_files[fd]); /// is arg type correct?
        curproc->p_files[fd] = NULL;
    }

    //NOT_YET_IMPLEMENTED("VFS: do_close");
    return 0;
}

/*
 * Duplicate the file descriptor fd.
 *
 * Return the new file descriptor on success, or:
 *  - EBADF: fd is invalid or not open
 *  - Propagate errors from get_empty_fd()
 *
 * Hint: Use get_empty_fd() to obtain an available file descriptor.
 */

long do_dup(int fd)
{
    if (fd < 0 || fd >= NFILES) {
        return -EBADF;
    } else { 
        if (curproc->p_files[fd] == NULL) {
        return -EBADF;
        }
    }
    
    int new_fd;

    file_t *file = fget(fd);

    long ret_fd = get_empty_fd(&new_fd); /// right arg?
    if (ret_fd != 0) {
        return ret_fd;
    }
    //fref(file);

    /// should i do this? :
    curproc->p_files[new_fd] = file;

    //NOT_YET_IMPLEMENTED("VFS: do_dup");
    return new_fd;
}

/*
 * Duplicate the file descriptor ofd using the new file descriptor nfd. If nfd
 * was previously open, close it.
 *
 * Return nfd on success, or:
 *  - EBADF: ofd is invalid or not open, or nfd is invalid
 *
 * Hint: You don't need to do anything if ofd and nfd are the same.
 * (If supporting MTP, this action must be atomic)
 */
long do_dup2(int ofd, int nfd) /// this is all right?
{
    if (ofd < 0 || ofd >= NFILES || nfd < 0 || nfd >= NFILES) { /// is this ok?
        return -EBADF;
    } else { 
        if (curproc->p_files[ofd] == NULL) {
        return -EBADF;
        }
    }
    if (ofd == nfd) {
        /// fput(&file); /// fput needed here?
        return nfd; /// this is all, right?
    }

    if (curproc->p_files[nfd] != NULL){
        long ret = do_close(nfd);
        if (ret != 0) {
            return ret;
        }
        //fput(&curproc->p_files[nfd]);
        // int ret = do_close(nfd);
        // if (ret != 0) {
        //     return ret;
        // }
    }

    //file_t *file = fget(ofd);

    curproc->p_files[nfd] = curproc->p_files[ofd];
    fref(curproc->p_files[nfd]);

    //NOT_YET_IMPLEMENTED("VFS: do_dup2");
    return nfd;
}

/*
 * Create a file specified by mode and devid at the location specified by path.
 *
 * Return 0 on success, or:
 *  - EINVAL: Mode is not S_IFCHR, S_IFBLK, or S_IFREG
 *  - Propagate errors from namev_open()
 *
 * Hints:
 *  - Create the file by calling namev_open() with the O_CREAT flag.
 *  - Be careful about refcounts after calling namev_open(). The newly created 
 *    vnode should have no references when do_mknod returns. The underlying 
 *    filesystem is responsible for maintaining references to the inode, which 
 *    will prevent it from being destroyed, even if the corresponding vnode is 
 *    cleaned up.
 *  - You don't need to handle EEXIST (this would be handled within namev_open, 
 *    but doing so would likely cause problems elsewhere)
 */
long do_mknod(const char *path, int mode, devid_t devid)
{
    if (mode != S_IFCHR && mode != S_IFBLK && mode != S_IFREG) {
        return -EINVAL;
    }
    /// need to lock these?
    vnode_t *base;
    vnode_t *res_vnode;
    // long namev_open(struct vnode *base, const char *path, int oflags, int mode, 
    // devid_t devid, struct vnode **res_vnode)
    int ret = namev_open(curproc->p_cwd, path, O_CREAT, mode, devid, &res_vnode); /// right args
    if (ret != 0) {
        return ret;
    }
    vput(&res_vnode);

    //NOT_YET_IMPLEMENTED("VFS: do_mknod");
    return 0;
}

/*
 * Create a directory at the location specified by path.
 *
 * Return 0 on success, or:
 *  - ENAMETOOLONG: The last component of path is too long
 *  - ENOTDIR: The parent of the directory to be created is not a directory
 *  - EEXIST: A file located at path already exists
 *  - Propagate errors from namev_dir(), namev_lookup(), and the vnode
 *    operation mkdir
 *
 * Hints:
 * 1) Use namev_dir() to find the parent of the directory to be created. /// passing in curproc->p_cwd as base right?
 * 2) Use namev_lookup() to check that the directory does not already exist.
 * 3) Use the vnode operation mkdir to create the directory.
 *  - Compare against NAME_LEN to determine if the basename is too long.
 *    Check out ramfs_mkdir() to confirm that the basename will be null-
 *    terminated.
 *  - Be careful about locking and refcounts after calling namev_dir() and
 *    namev_lookup().
 */
long do_mkdir(const char *path)
{
    long ret = 0;
    vnode_t *parent_vnode = NULL;
    const char *name = NULL;
    size_t namelen = 0;
    vnode_t *res_vnode = NULL;

    ret = namev_dir(curproc->p_cwd, path, &parent_vnode, &name, &namelen);
    if (ret != 0) {
        return ret;
    }

    // chck if parent is a directory
    if (!S_ISDIR((parent_vnode)->vn_mode)) {
        vput(&parent_vnode);
        return -ENOTDIR;
    } 
    if (namelen >= NAME_LEN) {
        vput(&parent_vnode);
        return -ENAMETOOLONG;
    }

    vlock(parent_vnode);
    long res = namev_lookup(parent_vnode, name, namelen, &res_vnode);

    if (res == 0){
        vput_locked(&parent_vnode);
        vput(&res_vnode);
        return -EEXIST;

        // if (strcmp(path[strlen(path)-1], "/")){
        //     if (S_ISDIR((*res_vnode)->vn_mode)){
        //         vput_locked(parent_vnode); /// check if null?
        //         return 0;
        //     } else {
        //         vput_locked(parent_vnode);
        //         return -ENOTDIR;
        //     }
        // }
    } else if (res == -ENOENT){
        //vunlock(&parent_vnode); 
        // if (*namelen > NAME_LEN){ /// checked earlier
        //     vput_locked(&parent_vnode);
        //     return -ENAMETOOLONG;
        // }
        long ret = parent_vnode->vn_ops->mkdir(parent_vnode, name, namelen, &res_vnode);
        if (ret < 0) {
            //if (parent_vnode != NULL) { // if statement may not be needed
            vput_locked(&parent_vnode);
        } else {
            vput(&res_vnode);
            vput_locked(&parent_vnode);
        }
        return ret;

    } else {
        vput_locked(&parent_vnode);
        return res;
    }

    // if (res == 0) { /// need this check? error propagation?
    //     vunlock(&parent_vnode);
    //     if (parent_vnode != NULL) {
    //         vput(&parent_vnode);
    //     }
    //     return -EEXIST;
    // } else if (res != -ENOENT) {/// 
    //     vunlock(&parent_vnode);
    //     if (parent_vnode != NULL) {
    //         vput(&parent_vnode);
    //     }
    //     return res;
    // } else {
    //     // in this case, Use the vnode operation mkdir to create the directory
    //     vunlock(&parent_vnode); /// unlock here correct?
    //     long ret = parent_vnode->vn_ops->mkdir(parent_vnode, name, namelen, &res_vnode);
    //     if (ret != 0) {
    //         if (parent_vnode != NULL) {
    //             vput(&parent_vnode);
    //         }
    //         return ret;
    //     }
    //     return 0;
    // }
    //NOT_YET_IMPLEMENTED("VFS: do_mkdir");
}

/*
 * Delete a directory at path.
 *
 * Return 0 on success, or:
 *  - EINVAL: Attempting to rmdir with "." as the final component
 *  - ENOTEMPTY: Attempting to rmdir with ".." as the final component
 *  - ENOTDIR: The parent of the directory to be removed is not a directory
 *  - ENAMETOOLONG: the last component of path is too long
 *  - Propagate errors from namev_dir() and the vnode operation rmdir
 *
 * Hints:
 *  - Use namev_dir() to find the parent of the directory to be removed.
 *  - Be careful about refcounts from calling namev_dir().
 *  - Use the parent directory's rmdir operation to remove the directory.
 *  - Lock/unlock the vnode when calling its rmdir operation.
 */
long do_rmdir(const char *path)
{
    // similar work to do_mkdir
    vnode_t *parent_vnode = NULL;
    const char *name = NULL;
    size_t namelen = 0;
    vnode_t *res_vnode = NULL;

    long ret = namev_dir(curproc->p_cwd, path, &parent_vnode, &name, &namelen);
    if (ret != 0) {
        // if (parent_vnode != NULL) {
        //     vput(&parent_vnode);
        // }
        return ret;
    }

    if (strcmp(name, ".") == 0) {
        //if (parent_vnode != NULL) {
        vput(&parent_vnode);
        //}
        return -EINVAL;
    }

    if (strcmp(name, "..") == 0) {
        //if (parent_vnode != NULL) {
        vput(&parent_vnode);
        //}
        return -ENOTEMPTY;
    }

    // chck if parent is a directory
    if (!S_ISDIR((parent_vnode)->vn_mode)) {
        //if (parent_vnode != NULL) { /// im putting this everywhere, is that ok?
        vput(&parent_vnode);
        //}
        return -ENOTDIR;
    }

    if (namelen > NAME_LEN) {
        //if (parent_vnode != NULL) {
        
        vput(&parent_vnode);
       //}
        return -ENAMETOOLONG;
    }
    vlock(parent_vnode);
    ret = parent_vnode->vn_ops->rmdir(parent_vnode, name, namelen);
    vunlock(parent_vnode);
    vput(&parent_vnode);
    return ret;



    // vlock(&parent_vnode);
    // long res = namev_lookup(parent_vnode, name, namelen, &res_vnode);
    // if (res != 0) {
    //     vunlock(&parent_vnode);
    //     //if (parent_vnode != NULL) {
    //     vput(&parent_vnode);
    //     //}
    //     return res;
    // } else {
    //     // in this case, Use the vnode operation mkdir to create the directory
    //      /// unlock here correct?, or after?
    //     long ret = parent_vnode->vn_ops->rmdir(parent_vnode, name, namelen, &res_vnode);
    //     vunlock(&parent_vnode);
    //     if (ret != 0) {
    //         if (parent_vnode != NULL) {
    //             vput(&parent_vnode);
    //         }
    //         return ret;
    //     }
    //     return 0;
    // }

    // NOT_YET_IMPLEMENTED("VFS: do_rmdir");
    // return -1;
}

/*
 * Remove the link between path and the file it refers to.
 *
 * Return 0 on success, or:
 *  - ENOTDIR: the parent of the file to be unlinked is not a directory
 *  - EPERM: the file to be unlinked is a directory 
 *  - ENAMETOOLONG: the last component of path is too long
 *  - Propagate errors from namev_dir() and the vnode operation unlink
 *
 * Hints:
 *  - Use namev_dir() and be careful about refcounts.
 *  - Use namev_lookup() to get the vnode for the file to be unlinked. 
 *  - Lock/unlock the parent directory when calling its unlink operation.
 */
long do_unlink(const char *path)
{
    // similar work to do_mkdir
    vnode_t *parent_vnode = NULL;
    const char *name = NULL;
    size_t namelen = 0;
    vnode_t *res_vnode = NULL;

    long ret = namev_dir(curproc->p_cwd, path, &parent_vnode, &name, &namelen);
    if (ret != 0) {
        //if (parent_vnode != NULL) {
            //vput(&parent_vnode); /// namevdir should tc of that
        //}
    return ret;
    } 
     
    if (!S_ISDIR((parent_vnode)->vn_mode)) {
       //if (parent_vnode != NULL) { /// im putting this everywhere, is that ok?
        vput(&parent_vnode);
        //}
        return -ENOTDIR;
    }
    if (namelen >= NAME_LEN) {
        //if (parent_vnode != NULL) {
        vput(&parent_vnode);
       // }
        return -ENAMETOOLONG;
    }

    vlock(parent_vnode);
    long res = namev_lookup(parent_vnode, name, namelen, &res_vnode);
    if (res != 0) {
        //vunlock(&parent_vnode);
        //if (parent_vnode != NULL) {
        vput_locked(&parent_vnode);
        //}
        // if (res_vnode != NULL) {
        //     vput(&res_vnode);
        // }
        return res;
    } else {
        // check if file is a directory
        if (S_ISDIR((res_vnode)->vn_mode)) {
            //vunlock(&parent_vnode);
            //if (parent_vnode != NULL) {
            vput_locked(&parent_vnode);
            //}
            // if res_vnode != NULL {
            vput(&res_vnode);
            // }
            return -EPERM;
        }
        vput(&res_vnode);
        // in this case, Use the vnode unlink operation 
        res = parent_vnode->vn_ops->unlink(parent_vnode, name, namelen); /// num of args, parent vnode not needed
        // if (res != 0) {
        //     //if (parent_vnode != NULL) {
        //     vunlock(&parent_vnode);
        //     //}
        //     if res_vnode != NULL {
        //         vput(&res_vnode);
        //     }
        //     return res;
        // }
        vput_locked(&parent_vnode);
        

        return res;
    }


    //ret = parent_vnode->vn_ops->unlink(parent_vnode, name, namelen, &res_vnode);

    


    //NOT_YET_IMPLEMENTED("VFS: do_unlink");
    //return -1;
}

/* 
 * Create a hard link newpath that refers to the same file as oldpath.
 *
 * Return 0 on success, or:
 *  - EPERM: oldpath refers to a directory
 *  - ENAMETOOLONG: The last component of newpath is too long
 *  - ENOTDIR: The parent of the file to be linked is not a directory
 *
 * Hints:
 * 1) Use namev_resolve() on oldpath to get the target vnode.
 * 2) Use namev_dir() on newpath to get the directory vnode.
 * 3) Use vlock_in_order() to lock the directory and target vnodes.
 * 4) Use the directory vnode's link operation to create a link to the target.
 * 5) Use vunlock_in_order() to unlock the vnodes.
 * 6) Make sure to clean up references added from calling namev_resolve() and
 *    namev_dir().
 */
long do_link(const char *oldpath, const char *newpath)
{
    // follow the instructions above in the function header
    vnode_t *old_vnode = NULL;
    vnode_t *new_vnode = NULL;
    const char *name = NULL;
    size_t namelen = 0;
    vnode_t *res_vnode = NULL;

    long ret = namev_resolve(curproc->p_cwd, oldpath, &old_vnode);
    if (ret != 0) {
        //if (old_vnode != NULL) { //// comment this entire if check out?
            //vput(&old_vnode);
        //}
        return ret;
    }
    if (S_ISDIR((old_vnode)->vn_mode)) {
        if (old_vnode != NULL) { /// comment the if wrapper out?
            vput(&old_vnode);
        }
        return -EPERM;
    }

    ret = namev_dir(curproc->p_cwd, newpath, &new_vnode, &name, &namelen);
    if (ret != 0) {
        if (old_vnode != NULL) { /// comment the if wrapper out?
            vput(&old_vnode);
        }
        // if (new_vnode != NULL) {
        //     vput(&new_vnode);
        // }
        return ret;
    }
    if (!S_ISDIR((new_vnode)->vn_mode)) { //// comment the if wrappers out? in the rest of the function?
        if (old_vnode != NULL) {
            vput(&old_vnode);
        }
        if (new_vnode != NULL) {
            vput(&new_vnode);
        }
        return -ENOTDIR;
    }
    if (namelen > NAME_LEN) {
        //if (old_vnode != NULL) {
        vput(&old_vnode);
       // }
        //if (new_vnode != NULL) {
        vput(&new_vnode);
        //}
        return -ENAMETOOLONG;
    }

    vlock_in_order(old_vnode, new_vnode);
    long res = new_vnode->vn_ops->link(new_vnode, name, namelen, old_vnode);
    vunlock_in_order(old_vnode, new_vnode);
  
    vput(&old_vnode);
    vput(&new_vnode);

    if (res != 0) {   
        return res;
    }
    return 0;
    //NOT_YET_IMPLEMENTED("VFS: do_link");
    //return -1;
}

/* Rename a file or directory.
 *
 * Return 0 on success, or:
 *  - ENOTDIR: the parent of either path is not a directory
 *  - ENAMETOOLONG: the last component of either path is too long
 *  - Propagate errors from namev_dir() and the vnode operation rename
 *
 * You DO NOT need to support renaming of directories.
 * Steps:
 * 1. namev_dir oldpath --> olddir vnode
 * 2. namev_dir newpath --> newdir vnode
 * 4. Lock the olddir and newdir in ancestor-first order (see `vlock_in_order`)
 * 5. Use the `rename` vnode operation
 * 6. Unlock the olddir and newdir
 * 8. vput the olddir and newdir vnodes
 *
 * ignore these comment section below
 * Alternatively, you can allow do_rename() to rename directories if
 * __RENAMEDIR__ is set in Config.mk. As with all extra credit
 * projects this is harder and you will get no extra credit (but you
 * will get our admiration). Please make sure the normal version works first.
 * Steps:
 * 1. namev_dir oldpath --> olddir vnode
 * 2. namev_dir newpath --> newdir vnode
 * 3. Lock the global filesystem `vnode_rename_mutex`
 * 4. Lock the olddir and newdir in ancestor-first order (see `vlock_in_order`)
 * 5. Use the `rename` vnode operation
 * 6. Unlock the olddir and newdir
 * 7. Unlock the global filesystem `vnode_rename_mutex`
 * 8. vput the olddir and newdir vnodes
 *
 * P.S. This scheme /probably/ works, but we're not 100% sure.
 */



long do_rename(const char *oldpath, const char *newpath)
{
    vnode_t *old_res_vnode = NULL;
    const char *old_name;
    size_t old_namelen;
    //vnode_t **res_vnode = NULL;

    vnode_t *new_res_vnode = NULL;
    const char *new_name;
    size_t new_namelen;

    long old_ret = namev_dir(curproc->p_cwd, oldpath, &old_res_vnode, &old_name, &old_namelen); /// is base right?
    if (old_ret != 0) {
        return old_ret;
    }

    long new_ret = namev_dir(curproc->p_cwd, newpath, &new_res_vnode, &new_name, &new_namelen);
    if (new_ret != 0) {
        vput(&old_res_vnode);
        return new_ret;
    }

    if (!S_ISDIR(old_res_vnode->vn_mode) || !S_ISDIR(new_res_vnode->vn_mode)) {
        vput(&old_res_vnode);
        vput(&new_res_vnode);
        return -ENOTDIR;
    }
    
    if (old_namelen > NAME_LEN || new_namelen > NAME_LEN) { /// separate if statements
        vput(&old_res_vnode);
        vput(&new_res_vnode);
        return -ENAMETOOLONG;
    }

    vlock_in_order(old_res_vnode, new_res_vnode);
    //(struct vnode *olddir, const char *oldname, size_t oldnamelen, struct vnode *newdir, const char *newname, size_t newnamelen)
    int ret = old_res_vnode->vn_ops->rename(old_res_vnode, old_name, old_namelen,  new_res_vnode, new_name, new_namelen);
    if (ret != 0) {
        return ret;
    }
    vunlock_in_order(old_res_vnode, new_res_vnode);
    vput(&old_res_vnode);
    vput(&new_res_vnode);
    //NOT_YET_IMPLEMENTED("VFS: do_rename");
    return 0;
}

/* Set the current working directory to the directory represented by path.
 *
 * Returns 0 on success, or:
 *  - ENOTDIR: path does not refer to a directory
 *  - Propagate errors from namev_resolve()
 *
 * Hints:
 *  - Use namev_resolve() to get the vnode corresponding to path.
 *  - Pay attention to refcounts!
 *  - Remember that p_cwd should not be locked upon return from this function.
 *  - (If doing MTP, must protect access to p_cwd)
 */
long do_chdir(const char *path) /// locks and refcounts??
{
    vnode_t *res_vnode = NULL;
    //vlock(curproc->p_cwd);
    long ret = namev_resolve(curproc->p_cwd, path, &res_vnode);
    if (ret != 0) {
        return ret;
    }
    // check if path is a directory
    if (!S_ISDIR(res_vnode->vn_mode)) {
        vput(&res_vnode);
        return -ENOTDIR;
    }
    vnode_t *old_vnode = curproc->p_cwd;
    curproc->p_cwd = res_vnode;
    vput(&old_vnode);
    
    //vunlock(curproc->p_cwd);

    //NOT_YET_IMPLEMENTED("VFS: do_chdir");
    return 0;
}

/*
 * Read a directory entry from the file specified by fd into dirp.
 *
 * Return sizeof(dirent_t) on success, or:
 *  - EBADF: fd is invalid or is not open
 *  - ENOTDIR: fd does not refer to a directory
 *  - Propagate errors from the vnode operation readdir
 *
 * Hints:
 *  - Use the vnode operation readdir.
 *  - Be sure to update file position according to readdir's return value.
 *  - On success (readdir return value is strictly positive), return
 *    sizeof(dirent_t).
 */
ssize_t do_getdent(int fd, struct dirent *dirp)
{
    file_t *file = fget(fd);
    // check if file is valid
    if (file == NULL) {
        return -EBADF;
    } else if (!S_ISDIR(file->f_vnode->vn_mode)) {
        fput(&file);
        return -ENOTDIR;
    }

    vlock(file->f_vnode); 
    int ret = file->f_vnode->vn_ops->readdir(file->f_vnode, file->f_pos, dirp);
    if (ret <= 0) {
        vunlock(file->f_vnode);
        fput(&file);
        return ret;
    }
    file->f_pos += ret;
    vunlock(file->f_vnode);
    fput(&file);

    return sizeof(dirent_t);
    // } else {
    //     return ret;
    // }

    //NOT_YET_IMPLEMENTED("VFS: do_getdent");
    // return -1;
}

/*
 * Set the position of the file represented by fd according to offset and
 * whence.
 *
 * Return the new file position, or:
 *  - EBADF: fd is invalid or is not open
 *  - EINVAL: whence is not one of SEEK_SET, SEEK_CUR, or SEEK_END;
 *            or, the resulting file offset would be negative
 *
 * Hints:
 *  - See `man 2 lseek` for details about whence.
 *  - Be sure to protect the vnode if you have to access its vn_len.
 */
off_t do_lseek(int fd, off_t offset, int whence)
{
    file_t *file = fget(fd);
    // check if file is valid
    if (file == NULL) {
        return -EBADF;
    }
    // } else if (!(file->f_mode & FMODE_READ)){
    //     fput(&file);
    //     return -EBADF;
    // }

    // check if whence is valid
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        fput(&file);
        return -EINVAL;
    }

    int pos;
    // handle whence
    if (whence == SEEK_SET) {
        pos = offset;
    } else if (whence == SEEK_CUR) {
        pos = file->f_pos + offset;
    } else if (whence == SEEK_END) {
        vlock(file->f_vnode);
        pos = file->f_vnode->vn_len + offset;
        vunlock(file->f_vnode); /// is this right?
    }

    if (pos < 0){
        fput(&file);
        return -EINVAL;
    }

    file->f_pos = pos;

    //NOT_YET_IMPLEMENTED("VFS: do_lseek");
    fput(&file); /// fput after anytimeyou call fget
    return file->f_pos;
}

/* Use buf to return the status of the file represented by path.
 *
 * Return 0 on success, or:
 *  - Propagate errors from namev_resolve() and the vnode operation stat.
 */
long do_stat(const char *path, stat_t *buf)
{
    vnode_t *res_vnode = NULL;

    //vlock(&curproc->p_mtx);
    long ret = namev_resolve(curproc->p_cwd, path, &res_vnode);
    if (ret != 0) {
        //vunlock(&curproc->p_mtx);
        return ret;
    }
    vlock(res_vnode);
    ret = res_vnode->vn_ops->stat(res_vnode, buf); //// type?
    if (ret != 0) {
        //vunlock(&curproc->p_mtx);
        vput_locked(&res_vnode);
        // vunlock(res_vnode);
        // vput(&res_vnode);
        return ret;
    }
    vput_locked(&res_vnode);

    //NOT_YET_IMPLEMENTED("VFS: do_stat");
    return 0;
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int do_mount(const char *source, const char *target, const char *type)
{
    NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
    return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not
 * worry about freeing the fs_t struct here, that is done in vfs_umount. All
 * this function does is figure out which file system to pass to vfs_umount and
 * do good error checking.
 */
int do_umount(const char *target)
{
    NOT_YET_IMPLEMENTED("MOUNTING: do_unmount");
    return -EINVAL;
}
#endif
