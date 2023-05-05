#include "globals.h"
#include "kernel.h"
#include <errno.h>

#include "vm/anon.h"
#include "vm/shadow.h"

#include "util/debug.h"
#include "util/printf.h"
#include "util/string.h"

#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/slab.h"
#include "mm/tlb.h"

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void vmmap_init(void)
{
    vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
    vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
    KASSERT(vmmap_allocator && vmarea_allocator);
}
/*In order to manage address spaces, you must maintain each process’ list of virtual memory areas. Each memory region is essentially a range 
of virtual page numbers, a memory object, and the page offset into that memory object that the first page of the virtual memory area 
corresponds to. These numbers are all stored at page resolution instead of byte (address) resolution because x86 paging manages memory at 
page granularity. Therefore, defining sub-page level permissions wouldn't make sense since they would not be enforced by the memory management 
unit (MMU).

For example a memory region could span from page 2000 to 2005, be backed by a memory object corresponding to a file and have offset 3. In this 
example, the virtual page 2000 (which is at address 2000*PAGE_SIZE) should contain the content of the 3rd page of the file (bytes 3*PAGE_SIZE 
to 4*PAGE_SIZE - 1).

You must keep the areas sorted by the start of their virtual page ranges and ensure that no two ranges overlap with each other. There will be 
several edge cases (which are better documented in the code) where you will have to unmap a section of a virtual memory area, which could 
require splitting an area or truncating the beginning or end of an area.

*/

/// any locking or counts in these functions?

/*
 * Allocate and initialize a new vmarea using vmarea_allocator.
 */
vmarea_t *vmarea_alloc(void)
{
    vmarea_t *vma = (vmarea_t *)slab_obj_alloc(vmarea_allocator);
    if (vma)
    // /*    size_t vma_start; /* [starting vfn, 
    // size_t vma_end;   /*  ending vfn) */
    // size_t vma_off;   /* offset from beginning of vma_obj in pages */
    //                   /* the reason this field is necessary is that 
    //                      when files are mmap'ed, it doesn't have 
    //                      to start from location 0. You could, for instance, 
    //                      map pages 10-15 of a file, and vma_off would be 10. */

    // int vma_prot;  /* permissions (protections) on mapping, see mman.h */
    // int vma_flags; /* either MAP_SHARED or MAP_PRIVATE. It can also specify 
    //                   MAP_ANON and MAP_FIXED */

    // struct vmmap *vma_vmmap; /* address space that this area belongs to */
    // struct mobj *vma_obj;    /* the memory object that corresponds to this address region */
    // list_link_t vma_plink;   /* link on process vmmap maps list */
    
    {
        memset(vma, 0, sizeof(vmarea_t));
        list_link_init(&vma->vma_plink);
        vma->vma_obj = NULL;
        vma->vma_vmmap = NULL; /// set these and any other fields?
    }
    return vma;

    //NOT_YET_IMPLEMENTED("VM: vmarea_alloc");
    //return NULL;
}

/*
 * Free the vmarea by removing it from any lists it may be on, putting its
 * vma_obj if it exists, and freeing the vmarea_t.
 */
void vmarea_free(vmarea_t *vma)
{
    if (list_link_is_linked(&vma->vma_plink)){
        list_remove(&vma->vma_plink);
    }
    //list_remove(&vma->vma_plink);
    mobj_lock(vma->vma_obj); /// locked?
    if (vma->vma_obj)
    {
        mobj_put_locked(&vma->vma_obj);
    } else {
        mobj_unlock(vma->vma_obj);
    }
    slab_obj_free(vmarea_allocator, vma); 

    //NOT_YET_IMPLEMENTED("VM: vmarea_free");
}

/*
 * Create and initialize a new vmmap. Initialize all the fields of vmmap_t.
 */
vmmap_t *vmmap_create(void)
{
    // vmmap_t *map = (vmmap_map *)slab_obj_alloc(vmmap_allocator);
    // if (map) /// any locking or counts?
    // {
    //     memset(map, 0, sizeof(vmmap_t));
    //     list_init(&map->vmm_list);
    //     map->vmm_proc = NULL;
    // }
    // return map;
    NOT_YET_IMPLEMENTED("VM: vmmap_create");
    return NULL;
}

/*
 * Destroy the map pointed to by mapp and set *mapp = NULL.
 * Remember to free each vma in the maps list.
 */
void vmmap_destroy(vmmap_t **mapp)
{
    // vmmap_t *map = *mapp;
    // vmarea_t *vma;
    // // use list_iterate to free each vma in the maps list
    // list_iterate(&(*ma)->vmm_list, vma, vmarea_t, vma_plink)
    // {
    //     vmarea_free(vma);
    // }
    // slab_obj_free(vmmap_allocator, *map); /// this is right?
    // *mapp = NULL;

//     //
//    // NOT_YET_IMPLEMENTED("VM: vmmap_destroy");
}

/*
 * Add a vmarea to an address space. Assumes (i.e. asserts to some extent) the
 * vmarea is valid. Iterate through the list of vmareas, and add it 
 * accordingly. 
 */
void vmmap_insert(vmmap_t *map, vmarea_t *new_vma)
{
    // asserts that new vma is valid
    vmarea_t *vma;
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    {
        if (new_vma->vma_start < vma->vma_start)
        {
            list_insert_before(&vma->vma_plink, &new_vma->vma_plink);
            return;
        }
    }
    list_insert_tail(&map->vmm_list, &new_vma->vma_plink);

    //NOT_YET_IMPLEMENTED("VM: vmmap_insert");
}

/*
 * Find a contiguous range of free virtual pages of length npages in the given
 * address space. Returns starting page number for the range, without altering the map.
 * Return -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is
 *    - VMMAP_DIR_HILO: a gap as high in the address space as possible, starting 
 *                      from USER_MEM_HIGH.  
 *    - VMMAP_DIR_LOHI: a gap as low in the address space as possible, starting 
 *                      from USER_MEM_LOW. 
 * 
 * Make sure you are converting between page numbers and addresses correctly! 
 * Think about using the list iterate macro to iterate through the vmareas in the vmmap. 
 * Also, when looking at the lower and upper page boundaries, make sure to convert USER_MEM_LOW 
 * and USER_MEM_HIGH to be in terms of page numbers!
 * 
 * If it is being called to get n pages, it should find a sequence of n pages that ends at USER_MEM_HIGH 
 * and return the page number of the lowest-numbered of those pages.
 */
ssize_t vmmap_find_range(vmmap_t *map, size_t npages, int dir)
{
    if (npages > (USER_MEM_HIGH - USER_MEM_LOW)/PAGE_SIZE)
    {
        return -1;
    }
    if (dir == VMMAP_DIR_HILO)
    {
        size_t vfn = ADDR_TO_PN(USER_MEM_HIGH) - npages;
        vmarea_t *vma;
        list_iterate_reverse(&map->vmm_list, vma, vmarea_t, vma_plink)
        {
            if (vma->vma_start > vfn)
            {
                return vfn;
            }
            else
            {
                vfn = vma->vma_end + 1;
            }
        }
        return vfn;
    }
    else if (dir == VMMAP_DIR_LOHI)
    {
        size_t vfn = ADDR_TO_PN(USER_MEM_LOW);
        vmarea_t *vma;
        list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
        {
            if (vma->vma_start- vfn >= npages)
            {
                return vfn;
            }
            else
            {
                vfn = vma->vma_end;
            }
        }
        return vfn;
    }
    else
    {
        return -1;
    }

    return -1;

    // vmarea_t *vma;
    // size_t start, end;
    // if (dir == VMMAP_DIR_HILO)
    // {
    //     start = USER_MEM_HIGH - npages;
    //     end = USER_MEM_LOW;
    // }
    // else
    // {
    //     start = USER_MEM_LOW;
    //     end = USER_MEM_HIGH - npages;
    // }
    
    // list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    // {
    //     if (vma->vma_start > start && vma->vma_end < end)
    //     {
    //         return vma->vma_start;
    //     }
    // }


    //NOT_YET_IMPLEMENTED("VM: vmmap_find_range");
   // return -1;
}

/*
 * Return the vm_area that vfn (a page number) lies in. Scan the address space looking
 * for a vma whose range covers vfn. If the page is unmapped, return NULL.
 */
vmarea_t *vmmap_lookup(vmmap_t *map, size_t vfn)
{
    if (vfn < ADDR_TO_PN(USER_MEM_LOW) || vfn > ADDR_TO_PN(USER_MEM_HIGH))
    {
        return NULL;
    }
    vmarea_t *vma;
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    {
        if (vma->vma_start <= vfn && vma->vma_end >= vfn)
        {
            return vma;
        }
    }
    return NULL;
    // NOT_YET_IMPLEMENTED("VM: vmmap_lookup");
    // return NULL;
}

/*
 * For each vmarea in the map, if it is a shadow object, call shadow_collapse.
 */
void vmmap_collapse(vmmap_t *map)
{
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    {
        if (vma->vma_obj->mo_type == MOBJ_SHADOW)
        {
            mobj_lock(vma->vma_obj);
            shadow_collapse(vma->vma_obj);
            mobj_unlock(vma->vma_obj);
        }
    }
}

/*
 * This is where the magic of fork's copy-on-write gets set up. 
 * 
 * Upon successful return, the new vmmap should be a clone of map with all 
 * shadow objects properly set up.
 *
 * For each vmarea, clone it's members. 
 *  1) vmarea is share-mapped, you don't need to do anything special. 
 *  2) vmarea is not share-mapped, time for shadow objects: 
 *     a) Create two shadow objects, one for map and one for the new vmmap you
 *        are constructing, both of which shadow the current vma_obj the vmarea
 *        being cloned. 
 *     b) After creating the shadow objects, put the original vma_obj
 *     c) and insert the shadow objects into their respective vma's.
 *
 * Be sure to clean up in any error case, manage the reference counts correctly,
 * and to lock/unlock properly.
 */
vmmap_t *vmmap_clone(vmmap_t *map) /// need help with this function /// any error cases? /// any locks
{
    vmmap_t *new_map = vmmap_create();
    if (new_map == NULL)
    {
        return NULL;
    }
    vmarea_t *vma;

    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    {
        vmarea_t *new_vma = vmarea_alloc();
        if (new_vma == NULL)
        {
            vmmap_destroy(new_map);
            return NULL;
        }
        new_vma->vma_start = vma->vma_start;
        new_vma->vma_end = vma->vma_end;
        new_vma->vma_off = vma->vma_off;
        new_vma->vma_prot = vma->vma_prot;
        new_vma->vma_flags = vma->vma_flags;
        new_vma->vma_obj = vma->vma_obj;
        new_vma->vma_vmmap = new_map;
        mobj_ref(new_vma->vma_obj);

        if (new_vma->vma_flags & MAP_SHARED)
        {
            list_insert_tail(&new_map->vmm_list, &new_vma->vma_plink); 
            continue;
        } 
        else
        {
            /// 
            mobj_t *new_shadow = shadow_create(new_vma->vma_obj); /// should be vma instead of new_vma? if yes, adjust unlocking too
            mobj_t *old_shadow = shadow_create(new_vma->vma_obj);
            if (new_shadow == NULL || old_shadow == NULL)
            {
                vmmap_destroy(new_map);
                return NULL;
            }
            mobj_put(&vma->vma_obj); /// this?
            new_vma->vma_obj = new_shadow;
            vma->vma_obj = old_shadow;

            list_insert_tail(&new_map->vmm_list, &new_vma->vma_plink);
            //list_insert_tail(&map->vmm_list, &vma->vma_plink); /// confirm
            //mobj_lock(new_vma->vma_obj);

        }

    }
    return new_map;
    //NOT_YET_IMPLEMENTED("VM: vmmap_clone");
    //return NULL;
}

/*
 *
 * Insert a mapping into the map starting at lopage for npages pages.
 * 
 *  file    - If provided, the vnode of the file to be mapped in
 *  lopage  - If provided, the desired start range of the mapping
 *  prot    - See mman.h for possible values
 *  flags   - See do_mmap()'s comments for possible values
 *  off     - Offset in the file to start mapping at, in bytes
 *  dir     - VMMAP_DIR_LOHI or VMMAP_DIR_HILO
 *  new_vma - If provided, on success, must point to the new vmarea_t
 * 
 *  Return 0 on success, or:
 *  - ENOMEM: On vmarea_alloc, annon_create, shadow_create or 
 *    vmmap_find_range failure 
 *  - Propagate errors from file->vn_ops->mmap and vmmap_remove
 * 
 * Hints:
 *  - You can assume/assert that all input is valid. It may help to write
 *    this function and do_mmap() somewhat in tandem.
 *  - If file is NULL, create an anon object.
 *  - If file is non-NULL, use the vnode's mmap operation to get the mobj.
 *    Do not assume it is file->vn_obj (mostly relevant for special devices).
 *  - If lopage is 0, use vmmap_find_range() to get a valid range
 *  - If lopage is nonzero and MAP_FIXED is specified and 
 *    the given range overlaps with any preexisting mappings, 
 *    remove the preexisting mappings.
 *  - If MAP_PRIVATE is specified, set up a shadow object. Be careful with
 *    refcounts!
 *  - Be careful: off is in bytes (albeit should be page-aligned), but
 *    vma->vma_off is in pages.
 *  - Be careful with the order of operations. Hold off on any irreversible
 *    work until there is no more chance of failure.
 */
long vmmap_map(vmmap_t *map, vnode_t *file, size_t lopage, size_t npages,
               int prot, int flags, off_t off, int dir, vmarea_t **new_vma) /// anon obj what to do
{
    KASSERT(map != NULL);

    if (file == NULL)
    {
        mobj_t *anon = anon_create();
        if (anon == NULL)
        {
            return -ENOMEM;
        }
        file->vn_mobj = *anon; /// type correct?
    }
    else
    {
        mobj_t *mobj;
        int result = file->vn_ops->mmap(file, &mobj);
        if (result < 0)
        {
            return result;
        }
        file->vn_mobj = *mobj; /// type correct?
    }

    if (lopage == 0)
    {
        int result = vmmap_find_range(map, npages, dir);
        if (result < 0)
        {
            return result;
        }
    } else {
        if (flags & MAP_FIXED)
        {
            // vmarea_t *vma;
            // list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink) 
            // {
            //     if (lopage >= vma->vma_start && lopage <= vma->vma_end)
            //     {
                    vmmap_remove(map, lopage, npages);
                //}
            }
        }
    



    // vmarea_t *vma = vmarea_alloc();
    // if (vma == NULL)
    // {
    //     return -ENOMEM;
    // }

    // vma->vma_start = lopage;
    // vma->vma_end = lopage + npages;
    // vma->vma_prot = prot;
    // vma->vma_flags = flags;
    // vma->vma_off = ADDR_TO_PN(off);
    // vma->vma_obj = &file->vn_mobj; //// could be anonymous!!
    // vma->vma_vmmap = map;
    // mobj_ref(vma->vma_obj);

    // if (flags & MAP_PRIVATE)
    // {
    //     mobj_t *shadow = shadow_create(vma->vma_obj);
    //     if (shadow == NULL)
    //     {
    //         return -ENOMEM;
    //     }
    //     vma->vma_obj = shadow;
    //     mobj_ref(vma->vma_obj);
    // }

    // list_insert_tail(&map->vmm_list, &vma->vma_plink);

    // if (new_vma != NULL)
    // {
    //     *new_vma = vma;
    // }

    return 0;
    // NOT_YET_IMPLEMENTED("VM: vmmap_map");
    // return -1;
}

/*
 * Iterate over the mapping's vmm_list and make sure that the specified range
 * is completely empty. You will have to handle the following cases:
 *
 * Key:     [             ] = existing vmarea_t
 *              *******     = region to be unmapped
 *
 * Case 1:  [   *******   ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. Be sure to increment the refcount of
 * the object associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 * 
 * Return 0 on success, or:
 *  - ENOMEM: Failed to allocate a new vmarea when splitting a vmarea (case 1).
 * 
 * Hints:
 *  - Whenever you shorten/remove any mappings, be sure to call pt_unmap_range()
 *    tlb_flush_range() to clean your pagetables and TLB.
 */
long vmmap_remove(vmmap_t *map, size_t lopage, size_t npages)
{
    NOT_YET_IMPLEMENTED("VM: vmmap_remove");
    return -1;

    // vmarea_t *vma;
    // list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    // {
    //     // if (lopage >= vma->vma_start && lopage <= vma->vma_end) 
        // {
        //     if (lopage == vma->vma_start && lopage + npages == vma->vma_end) 
        //     {
        //         list_remove(&vma->vma_plink);
        //         pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(lopage), npages);
        //         tlb_flush_range((uintptr_t)PN_TO_ADDR(lopage), npages);
        //         mobj_t *obj = vma->vma_obj;
        //         mobj_put(obj);
        //         vmarea_free(vma);
        //         return 0;
        //     }
        //     else if (lopage == vma->vma_start)
        //     {
        //         vma->vma_start += npages;
        //         vma->vma_off += npages;
        //         vma->vma_obj->mmo_ops->ref(vma->vma_obj);
        //         pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(lopage), npages);
        //         tlb_flush_range((uintptr_t)PN_TO_ADDR(lopage), npages);
        //         return 0;
        //     }
        //     else if (lopage + npages == vma->vma_end)
        //     {
        //         vma->vma_end -= npages;
        //         pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(lopage), npages);
        //         tlb_flush_range((uintptr_t)PN_TO_ADDR(lopage), npages);
        //         return 0;
        //     }
        //     else
        //     {
        //         vmarea_t *new_vma = vmarea_alloc();
        //         if (new_vma == NULL)
        //         {
        //             return -ENOMEM;
        //         }
        //         new_vma->vma_start = lopage + npages;
        //         new_vma->vma_end = vma->vma_end;
        //         new_vma->vma_prot = vma->vma_prot;
        //         new_vma->vma_flags = vma->vma_flags;
        //         new_vma->vma_off = vma->vma_off + npages;
        //         new_vma->vma_obj = vma->vma_obj;
        //         new_vma


        //             vma->vma_end = lopage;}

    //     // }

    //     // if overlap
    //     if (vma->vma_start >= (lopage + npages) || vma->vma_end <= lopage) 
    //     {
    //         return 0;
    //     }

    //     if (vma->vma_start >= lopage && vma->vma_end <= (lopage + npages))
    //     {
    //         vmarea_free(vma);
    //         if (vma->vma_proc != NULL)
    //         {
    //             pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(lopage), npages);
    //             tlb_flush_range((uintptr_t)PN_TO_ADDR(lopage), npages);
    //         }
    //         //list_remove(&vma->vma_plink);
            
    //         // mobj_t *obj = vma->vma_obj; ///
    //         // mobj_put(obj);
            
    //         return 0;
    //     }

    //     if (vma->vma_start >= lopage && vma->vma_end > (lopage + npages) && (lopage + npages) > vma->vma_start) // maybe && lopage + npoages vma-> start
    //     {
    //         vma->vma_start = lopage + npages;
    //         vma->vma_off += npages + lopage - vma->vma_start;
    //         //vma->vma_obj->mmo_ops->ref(vma->vma_obj);
    //         if (vma->vma_proc != NULL)
    //         {
    //             pt_unmap_range(vma->vma_proc->p_p
    //             tlb_flush_range((uintptr_t)PN_TO_ADDR(lopage), npages);
    //         }
    //         // pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(lopage), npages);
    //         // tlb_flush_range((uintptr_t)PN_TO_ADDR(lopage), npages);
    //         return 0;
    //     }

    // }
    // list_iterate_end();

    // return 0;
}

/*
 * Returns 1 if the given address space has no mappings for the given range,
 * 0 otherwise.
 */
long vmmap_is_range_empty(vmmap_t *map, size_t startvfn, size_t npages)
{

    // vmarea_t *vma;
    // list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    // {
    //     if (vma->vma_start <= startvfn && vma->vma_end >= startvfn + npages)
    //     {
    //         return 0;
    //     }
    // }
    // return 1;

    NOT_YET_IMPLEMENTED("VM: vmmap_is_range_empty");
    return 0;
}

/*
 * Read into 'buf' from the virtual address space of 'map'. Start at 'vaddr'
 * for size 'count'. 'vaddr' is not necessarily page-aligned. count is in bytes.
 * 
 * Hints:
 *  1) Find the vmareas that correspond to the region to read from.
 *  2) Find the pframes within those vmareas corresponding to the virtual 
 *     addresses you want to read.
 *  3) Read from those page frames and copy it into `buf`.
 *  4) You will not need to check the permissisons of the area.
 *  5) You may assume/assert that all areas exist.
 * 
 * Return 0 on success, -errno on error (propagate from the routines called).
 * This routine will be used within copy_from_user(). 
 */
long vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count)
{
    // vmarea_t *vma;

    // list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    // {
    //     if (vma->vma_start <= ADDR_TO_PN(vaddr) && vma->vma_end >= ADDR_TO_PN(vaddr) + ADDR_TO_PN(count))
    //     {
    //         pframe_t *pf;
    //         size_t offset = ADDR_TO_PN(vaddr) - vma->vma_start;
    //         size_t size = ADDR_TO_PN(count);
    //         for (size_t i = 0; i < size; i++)
    //         {
    //             pf = pframe_get(vma->vma_obj, vma->vma_off + offset + i);
    //             memcpy(buf + i, pf->pf_addr, PAGE_SIZE);
    //             pframe_put(pf);
    //         }
    //         return 0;
    //     }
    // }


    NOT_YET_IMPLEMENTED("VM: vmmap_read");
    return 0;
}

/*
 * Write from 'buf' into the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'.
 * 
 * Hints:
 *  1) Find the vmareas to write to.
 *  2) Find the correct pframes within those areas that contain the virtual addresses
 *     that you want to write data to.
 *  3) Write to the pframes, copying data from buf.
 *  4) You do not need check permissions of the areas you use.
 *  5) Assume/assert that all areas exist.
 *  6) Remember to dirty the pages that you write to. 
 * 
 * Returns 0 on success, -errno on error (propagate from the routines called).
 * This routine will be used within copy_to_user(). 
 */
long vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count)
{
    NOT_YET_IMPLEMENTED("VM: vmmap_write");
    return 0;
}

size_t vmmap_mapping_info(const void *vmmap, char *buf, size_t osize)
{
    return vmmap_mapping_info_helper(vmmap, buf, osize, "");
}

size_t vmmap_mapping_info_helper(const void *vmmap, char *buf, size_t osize,
                                 char *prompt)
{
    KASSERT(0 < osize);
    KASSERT(NULL != buf);
    KASSERT(NULL != vmmap);

    vmmap_t *map = (vmmap_t *)vmmap;
    ssize_t size = (ssize_t)osize;

    int len =
        snprintf(buf, (size_t)size, "%s%37s %5s %7s %18s %11s %23s\n", prompt,
                 "VADDR RANGE", "PROT", "FLAGS", "MOBJ", "OFFSET", "VFN RANGE");

    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink)
    {
        size -= len;
        buf += len;
        if (0 >= size)
        {
            goto end;
        }

        len =
            snprintf(buf, (size_t)size,
                     "%s0x%p-0x%p  %c%c%c  %7s 0x%p %#.9lx %#.9lx-%#.9lx\n",
                     prompt, (void *)(vma->vma_start << PAGE_SHIFT),
                     (void *)(vma->vma_end << PAGE_SHIFT),
                     (vma->vma_prot & PROT_READ ? 'r' : '-'),
                     (vma->vma_prot & PROT_WRITE ? 'w' : '-'),
                     (vma->vma_prot & PROT_EXEC ? 'x' : '-'),
                     (vma->vma_flags & MAP_SHARED ? " SHARED" : "PRIVATE"),
                     vma->vma_obj, vma->vma_off, vma->vma_start, vma->vma_end);
    }

end:
    if (size <= 0)
    {
        size = osize;
        buf[osize - 1] = '\0';
    }
    return osize - size;
}
