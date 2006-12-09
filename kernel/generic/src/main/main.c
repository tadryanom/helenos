/*
 * Copyright (C) 2001-2004 Jakub Jermar
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

/** @addtogroup main
 * @{
 */

/**
 * @file
 * @brief 	Main initialization kernel function for all processors.
 *
 * During kernel boot, all processors, after architecture dependent
 * initialization, start executing code found in this file. After
 * bringing up all subsystems, control is passed to scheduler().
 *
 * The bootstrap processor starts executing main_bsp() while
 * the application processors start executing main_ap().
 *
 * @see scheduler()
 * @see main_bsp()
 * @see main_ap()
 */

#include <arch/asm.h>
#include <context.h>
#include <print.h>
#include <panic.h>
#include <debug.h>
#include <config.h>
#include <time/clock.h>
#include <proc/scheduler.h>
#include <proc/thread.h>
#include <proc/task.h>
#include <main/kinit.h>
#include <main/version.h>
#include <console/kconsole.h>
#include <cpu.h>
#include <align.h>
#include <interrupt.h>
#include <arch/mm/memory_init.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <genarch/mm/page_pt.h>
#include <mm/tlb.h>
#include <mm/as.h>
#include <mm/slab.h>
#include <synch/waitq.h>
#include <synch/futex.h>
#include <arch/arch.h>
#include <arch.h>
#include <arch/faddr.h>
#include <typedefs.h>
#include <ipc/ipc.h>
#include <macros.h>
#include <adt/btree.h>
#include <console/klog.h>
#include <smp/smp.h>
#include <ddi/ddi.h>

/** Global configuration structure. */
config_t config;

/** Initial user-space tasks */
init_t init = {
	0
};

/** Boot allocations. */
ballocs_t ballocs = {
	.base = NULL,
	.size = 0
};

context_t ctx;

/*
 * These 'hardcoded' variables will be intialized by
 * the linker or the low level assembler code with
 * appropriate sizes and addresses.
 */
uintptr_t hardcoded_load_address = 0;	/**< Virtual address of where the kernel
					  *  is loaded. */
size_t hardcoded_ktext_size = 0;	/**< Size of the kernel code in bytes.
					  */
size_t hardcoded_kdata_size = 0;	/**< Size of the kernel data in bytes.
					 */
uintptr_t stack_safe = 0;		/**< Lowest safe stack virtual address.
					  */

void main_bsp(void);
void main_ap(void);

/*
 * These two functions prevent stack from underflowing during the
 * kernel boot phase when SP is set to the very top of the reserved
 * space. The stack could get corrupted by a fooled compiler-generated
 * pop sequence otherwise.
 */
static void main_bsp_separated_stack(void);
#ifdef CONFIG_SMP
static void main_ap_separated_stack(void);
#endif

#define CONFIG_STACK_SIZE	((1<<STACK_FRAMES)*STACK_SIZE)

/** Main kernel routine for bootstrap CPU.
 *
 * Initializes the kernel by bootstrap CPU.
 * This function passes control directly to
 * main_bsp_separated_stack().
 *
 * Assuming interrupts_disable().
 *
 */
void main_bsp(void)
{
	config.cpu_count = 1;
	config.cpu_active = 1;
	
	config.base = hardcoded_load_address;
	config.memory_size = get_memory_size();
	
	config.kernel_size = ALIGN_UP(hardcoded_ktext_size +
		hardcoded_kdata_size, PAGE_SIZE);
	config.stack_size = CONFIG_STACK_SIZE;
	
	/* Initialy the stack is placed just after the kernel */
	config.stack_base = config.base + config.kernel_size;
	
	/* Avoid placing stack on top of init */
	count_t i;
	for (i = 0; i < init.cnt; i++) {
		if (PA_overlaps(config.stack_base, config.stack_size,
			init.tasks[i].addr, init.tasks[i].size))
			config.stack_base = ALIGN_UP(init.tasks[i].addr +
				init.tasks[i].size, config.stack_size);
	}

	/* Avoid placing stack on top of boot allocations. */
	if (ballocs.size) {
		if (PA_overlaps(config.stack_base, config.stack_size,
			ballocs.base, ballocs.size))
			config.stack_base = ALIGN_UP(ballocs.base +
				ballocs.size, PAGE_SIZE);
	}
	
	if (config.stack_base < stack_safe)
		config.stack_base = ALIGN_UP(stack_safe, PAGE_SIZE);
	
	context_save(&ctx);
	context_set(&ctx, FADDR(main_bsp_separated_stack), config.stack_base,
		THREAD_STACK_SIZE);
	context_restore(&ctx);
	/* not reached */
}


/** Main kernel routine for bootstrap CPU using new stack.
 *
 * Second part of main_bsp().
 *
 */
void main_bsp_separated_stack(void) 
{
	task_t *k;
	thread_t *t;
	count_t i;
	
	the_initialize(THE);

	/*
	 * kconsole data structures must be initialized very early
	 * because other subsystems will register their respective
	 * commands.
	 */
	kconsole_init();
	
	/*
	 * Exception handler initialization, before architecture
	 * starts adding its own handlers
	 */
	exc_init();

	/*
	 * Memory management subsystems initialization.
	 */	
	arch_pre_mm_init();
	frame_init();		
	/* Initialize at least 1 memory segment big enough for slab to work. */
	slab_cache_init();
	btree_init();
	as_init();
	page_init();
	tlb_init();
	ddi_init();
	arch_post_mm_init();

	version_print();
	printf("kernel: %.*p hardcoded_ktext_size=%zdK, "
		"hardcoded_kdata_size=%zdK\n", sizeof(uintptr_t) * 2,
		config.base, hardcoded_ktext_size >> 10, hardcoded_kdata_size >>
		10);
	printf("stack:  %.*p size=%zdK\n", sizeof(uintptr_t) * 2,
		config.stack_base, config.stack_size >> 10);

	arch_pre_smp_init();
	smp_init();
	/* Slab must be initialized after we know the number of processors. */
	slab_enable_cpucache();

	printf("config.memory_size=%zdM\n", config.memory_size >> 20);
	printf("config.cpu_count=%zd\n", config.cpu_count);
	cpu_init();
	
	calibrate_delay_loop();
	clock_counter_init();
	timeout_init();
	scheduler_init();
	task_init();
	thread_init();
	futex_init();
	klog_init();
	
	if (init.cnt > 0) {
		for (i = 0; i < init.cnt; i++)
			printf("init[%zd].addr=%.*p, init[%zd].size=%zd\n", i,
				sizeof(uintptr_t) * 2, init.tasks[i].addr, i,
				init.tasks[i].size);
	} else
		printf("No init binaries found\n");
	
	ipc_init();

	/*
	 * Create kernel task.
	 */
	k = task_create(AS_KERNEL, "KERNEL");
	if (!k)
		panic("can't create kernel task\n");
	
	/*
	 * Create the first thread.
	 */
	t = thread_create(kinit, NULL, k, 0, "kinit");
	if (!t)
		panic("can't create kinit thread\n");
	thread_ready(t);
	
	/*
	 * This call to scheduler() will return to kinit,
	 * starting the thread of kernel threads.
	 */
	scheduler();
	/* not reached */
}


#ifdef CONFIG_SMP
/** Main kernel routine for application CPUs.
 *
 * Executed by application processors, temporary stack
 * is at ctx.sp which was set during BSP boot.
 * This function passes control directly to
 * main_ap_separated_stack().
 *
 * Assuming interrupts_disable()'d.
 *
 */
void main_ap(void)
{
	/*
	 * Incrementing the active CPU counter will guarantee that the
	 * *_init() functions can find out that they need to
	 * do initialization for AP only.
	 */
	config.cpu_active++;

	/*
	 * The THE structure is well defined because ctx.sp is used as stack.
	 */
	the_initialize(THE);
	
	arch_pre_mm_init();
	frame_init();
	page_init();
	tlb_init();
	arch_post_mm_init();
	
	cpu_init();
	calibrate_delay_loop();
	arch_post_cpu_init();

	the_copy(THE, (the_t *) CPU->stack);

	/*
	 * If we woke kmp up before we left the kernel stack, we could
	 * collide with another CPU coming up. To prevent this, we
	 * switch to this cpu's private stack prior to waking kmp up.
	 */
	context_set(&CPU->saved_context, FADDR(main_ap_separated_stack),
		(uintptr_t) CPU->stack, CPU_STACK_SIZE);
	context_restore(&CPU->saved_context);
	/* not reached */
}


/** Main kernel routine for application CPUs using new stack.
 *
 * Second part of main_ap().
 *
 */
void main_ap_separated_stack(void)
{
	/*
	 * Configure timeouts for this cpu.
	 */
	timeout_init();

	waitq_wakeup(&ap_completion_wq, WAKEUP_FIRST);
	scheduler();
	/* not reached */
}
#endif /* CONFIG_SMP */

/** @}
 */
