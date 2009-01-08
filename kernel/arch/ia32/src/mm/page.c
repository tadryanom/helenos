/*
 * Copyright (c) 2001-2004 Jakub Jermar
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

/** @addtogroup ia32mm
 * @{
 */
/** @file
 */

#include <arch/mm/page.h>
#include <genarch/mm/page_pt.h>
#include <arch/mm/frame.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <mm/as.h>
#include <arch/types.h>
#include <align.h>
#include <config.h>
#include <func.h>
#include <arch/interrupt.h>
#include <arch/asm.h>
#include <debug.h>
#include <memstr.h>
#include <print.h>
#include <interrupt.h>

void page_arch_init(void)
{
	uintptr_t cur;
	int flags;

	if (config.cpu_active == 1) {
		page_mapping_operations = &pt_mapping_operations;
	
		/*
		 * PA2KA(identity) mapping for all frames until last_frame.
		 */
		for (cur = 0; cur < last_frame; cur += FRAME_SIZE) {
			flags = PAGE_CACHEABLE | PAGE_WRITE;
			if ((PA2KA(cur) >= config.base) && (PA2KA(cur) < config.base + config.kernel_size))
				flags |= PAGE_GLOBAL;
			page_mapping_insert(AS_KERNEL, PA2KA(cur), cur, flags);
		}

		exc_register(14, "page_fault", (iroutine) page_fault);
		write_cr3((uintptr_t) AS_KERNEL->genarch.page_table);
	} else
		write_cr3((uintptr_t) AS_KERNEL->genarch.page_table);

	paging_on();
}


uintptr_t hw_map(uintptr_t physaddr, size_t size)
{
	if (last_frame + ALIGN_UP(size, PAGE_SIZE) > KA2PA(KERNEL_ADDRESS_SPACE_END_ARCH))
		panic("Unable to map physical memory %p (%d bytes).", physaddr, size)
	
	uintptr_t virtaddr = PA2KA(last_frame);
	pfn_t i;
	for (i = 0; i < ADDR2PFN(ALIGN_UP(size, PAGE_SIZE)); i++) {
		uintptr_t addr = PFN2ADDR(i);
		page_mapping_insert(AS_KERNEL, virtaddr + addr, physaddr + addr, PAGE_NOT_CACHEABLE | PAGE_WRITE);
	}
	
	last_frame = ALIGN_UP(last_frame + size, FRAME_SIZE);
	
	return virtaddr;
}

void page_fault(int n __attribute__((unused)), istate_t *istate)
{
	uintptr_t page;
	pf_access_t access;
	
	page = read_cr2();
		
	if (istate->error_word & PFERR_CODE_RSVD)
		panic("Reserved bit set in page directory.");

	if (istate->error_word & PFERR_CODE_RW)
		access = PF_ACCESS_WRITE;
	else
		access = PF_ACCESS_READ;
	
	if (as_page_fault(page, access, istate) == AS_PF_FAULT) {
		fault_if_from_uspace(istate, "Page fault: %#x.", page);
		
		decode_istate(istate);
		printf("page fault address: %#lx\n", page);
		panic("Page fault.");
	}
}

/** @}
 */
