/*
 * Copyright (C) 2006 Jakub Jermar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup genericddi
 * @{
 */
 
/**
 * @file
 * @brief	Device Driver Interface functions.
 *
 * This file contains functions that comprise the Device Driver Interface.
 * These are the functions for mapping physical memory and enabling I/O
 * space to tasks.
 */

#include <ddi/ddi.h>
#include <ddi/ddi_arg.h>
#include <proc/task.h>
#include <security/cap.h>
#include <mm/frame.h>
#include <mm/as.h>
#include <synch/spinlock.h>
#include <syscall/copy.h>
#include <adt/btree.h>
#include <arch.h>
#include <align.h>
#include <errno.h>

/** This lock protects the parea_btree. */
SPINLOCK_INITIALIZE(parea_lock);

/** B+tree with enabled physical memory areas. */
static btree_t parea_btree;

/** Initialize DDI. */
void ddi_init(void)
{
	btree_create(&parea_btree);
}

/** Enable piece of physical memory for mapping by physmem_map().
 *
 * @param parea Pointer to physical area structure.
 *
 * @todo This function doesn't check for overlaps. It depends on the kernel to
 * create disjunct physical memory areas.
 */
void ddi_parea_register(parea_t *parea)
{
	ipl_t ipl;

	ipl = interrupts_disable();
	spinlock_lock(&parea_lock);
	
	/*
	 * TODO: we should really check for overlaps here.
	 * However, we should be safe because the kernel is pretty sane and
	 * memory of different devices doesn't overlap. 
	 */
	btree_insert(&parea_btree, (btree_key_t) parea->pbase, parea, NULL);

	spinlock_unlock(&parea_lock);
	interrupts_restore(ipl);	
}

/** Map piece of physical memory into virtual address space of current task.
 *
 * @param pf Physical address of the starting frame.
 * @param vp Virtual address of the starting page.
 * @param pages Number of pages to map.
 * @param flags Address space area flags for the mapping.
 *
 * @return 0 on success, EPERM if the caller lacks capabilities to use this
 * 	syscall, ENOENT if there is no task matching the specified ID or the
 * 	physical address space is not enabled for mapping and ENOMEM if there
 * 	was a problem in creating address space area. ENOTSUP is returned when
 * 	an attempt to create an illegal address alias is detected.
 */
static int ddi_physmem_map(uintptr_t pf, uintptr_t vp, count_t pages, int flags)
{
	ipl_t ipl;
	cap_t caps;
	mem_backend_data_t backend_data;

	backend_data.base = pf;
	backend_data.frames = pages;
	
	/*
	 * Make sure the caller is authorised to make this syscall.
	 */
	caps = cap_get(TASK);
	if (!(caps & CAP_MEM_MANAGER))
		return EPERM;

	ipl = interrupts_disable();

	/*
	 * Check if the physical memory area is enabled for mapping.
	 * If the architecture supports virtually indexed caches, intercept
	 * attempts to create an illegal address alias.
	 */
	spinlock_lock(&parea_lock);
	parea_t *parea;
	btree_node_t *nodep;
	parea = btree_search(&parea_btree, (btree_key_t) pf, &nodep);
	if (!parea || parea->frames < pages || ((flags & AS_AREA_CACHEABLE) &&
		!parea->cacheable) || (!(flags & AS_AREA_CACHEABLE) &&
		parea->cacheable)) {
		/*
		 * This physical memory area cannot be mapped.
		 */
		spinlock_unlock(&parea_lock);
		interrupts_restore(ipl);
		return ENOENT;
	}

#ifdef CONFIG_VIRT_IDX_DCACHE
	if (PAGE_COLOR(parea->vbase) != PAGE_COLOR(vp)) {
		/*
		 * Refuse to create an illegal address alias.
		 */
		spinlock_unlock(&parea_lock);
		interrupts_restore(ipl);
		return ENOTSUP;
	}
#endif /* CONFIG_VIRT_IDX_DCACHE */

	spinlock_unlock(&parea_lock);

	spinlock_lock(&TASK->lock);
	
	if (!as_area_create(TASK->as, flags, pages * PAGE_SIZE, vp, AS_AREA_ATTR_NONE,
		&phys_backend, &backend_data)) {
		/*
		 * The address space area could not have been created.
		 * We report it using ENOMEM.
		 */
		spinlock_unlock(&TASK->lock);
		interrupts_restore(ipl);
		return ENOMEM;
	}
	
	/*
	 * Mapping is created on-demand during page fault.
	 */
	
	spinlock_unlock(&TASK->lock);
	interrupts_restore(ipl);
	return 0;
}

/** Enable range of I/O space for task.
 *
 * @param id Task ID of the destination task.
 * @param ioaddr Starting I/O address.
 * @param size Size of the enabled I/O space..
 *
 * @return 0 on success, EPERM if the caller lacks capabilities to use this
 * 	syscall, ENOENT if there is no task matching the specified ID.
 */
static int ddi_iospace_enable(task_id_t id, uintptr_t ioaddr, size_t size)
{
	ipl_t ipl;
	cap_t caps;
	task_t *t;
	int rc;
	
	/*
	 * Make sure the caller is authorised to make this syscall.
	 */
	caps = cap_get(TASK);
	if (!(caps & CAP_IO_MANAGER))
		return EPERM;
	
	ipl = interrupts_disable();
	spinlock_lock(&tasks_lock);
	
	t = task_find_by_id(id);
	
	if ((!t) || (!context_check(CONTEXT, t->context))) {
		/*
		 * There is no task with the specified ID
		 * or the task belongs to a different security
		 * context.
		 */
		spinlock_unlock(&tasks_lock);
		interrupts_restore(ipl);
		return ENOENT;
	}

	/* Lock the task and release the lock protecting tasks_btree. */
	spinlock_lock(&t->lock);
	spinlock_unlock(&tasks_lock);

	rc = ddi_iospace_enable_arch(t, ioaddr, size);
	
	spinlock_unlock(&t->lock);
	interrupts_restore(ipl);
	return rc;
}

/** Wrapper for SYS_PHYSMEM_MAP syscall.
 *
 * @param phys_base Physical base address to map
 * @param virt_base Destination virtual address
 * @param pages Number of pages
 * @param flags Flags of newly mapped pages
 *
 * @return 0 on success, otherwise it returns error code found in errno.h
 */ 
unative_t sys_physmem_map(unative_t phys_base, unative_t virt_base, unative_t
	pages, unative_t flags)
{
	return (unative_t) ddi_physmem_map(ALIGN_DOWN((uintptr_t) phys_base,
		FRAME_SIZE), ALIGN_DOWN((uintptr_t) virt_base, PAGE_SIZE),
		(count_t) pages, (int) flags);
}

/** Wrapper for SYS_ENABLE_IOSPACE syscall.
 *
 * @param uspace_io_arg User space address of DDI argument structure.
 *
 * @return 0 on success, otherwise it returns error code found in errno.h
 */ 
unative_t sys_iospace_enable(ddi_ioarg_t *uspace_io_arg)
{
	ddi_ioarg_t arg;
	int rc;
	
	rc = copy_from_uspace(&arg, uspace_io_arg, sizeof(ddi_ioarg_t));
	if (rc != 0)
		return (unative_t) rc;
		
	return (unative_t) ddi_iospace_enable((task_id_t) arg.task_id,
		(uintptr_t) arg.ioaddr, (size_t) arg.size);
}

/** Disable or enable preemption.
 *
 * @param enable If non-zero, the preemption counter will be decremented,
 * 	leading to potential enabling of preemption. Otherwise the preemption
 * 	counter will be incremented, preventing preemption from occurring.
 *
 * @return Zero on success or EPERM if callers capabilities are not sufficient.
 */ 
unative_t sys_preempt_control(int enable)
{
        if (! cap_get(TASK) & CAP_PREEMPT_CONTROL)
                return EPERM;
        if (enable)
                preemption_enable();
        else
                preemption_disable();
        return 0;
}

/** @}
 */
