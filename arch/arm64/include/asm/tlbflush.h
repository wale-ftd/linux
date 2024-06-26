/*
 * Based on arch/arm/include/asm/tlbflush.h
 *
 * Copyright (C) 1999-2003 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_TLBFLUSH_H
#define __ASM_TLBFLUSH_H

#ifndef __ASSEMBLY__

#include <linux/mm_types.h>
#include <linux/sched.h>
#include <asm/cputype.h>
#include <asm/mmu.h>

/*
 * ARM64 架构提供了一条 TLB 失效指令： TLBI <type><level>{IS} {, <Xt>}
 *   1.字段<type>的常见选项如下：
 *     (1)ALL ：所有表项。
 *     (2)VMALL ：当前虚拟机的阶段 1 的所有表项，即表项的 VMID 是当前虚拟机的
 *               VMID 。虚拟机里面运行的客户操作系统的虚拟地址转换成物理地址分两
 *               个阶段：第 1 阶段把虚拟地址转换成中间物理地址，第 2 阶段把中间
 *               物理地址转换成物理地址。
 *     (3)VMALLS12 ：当前虚拟机的阶段 1 和阶段 2 的所有表项。
 *     (4)ASID ：匹配寄存器 Xt 指定的 ASID 的表项。
 *     (5)VA ：匹配寄存器 Xt 指定的虚拟地址和 ASID 的表项。
 *     (6)VAA ：匹配寄存器 Xt 指定的虚拟地址并且 ASID 可以是任意值的表项。
 *   2.字段<level>指定异常级别，取值如下：
 *     (1)E1 ：异常级别 1 。
 *     (2)E2 ：异常级别 2 。
 *     (3)E3 ：异常级别 3 。
 *   3.字段 IS 表示内部共享(Inner Shareable)，即多个核共享。如果不使用字段 IS ，
 *     表示非共享(或者使用 ns(non-shareable))，只被一个核使用。在 SMP 系统中，如
 *     果指令 TLBI 不携带字段 IS ，仅仅使当前核的 TLB 表项失效；如果指令 TLBI
 *     携带字段 IS ，表示使所有核的 TLB 表项失效。
 *   4.字段 Xt 是 X0～X31 中的任何一个寄存器。
 */

/*
 * Raw TLBI operations.
 *
 * Where necessary, use the __tlbi() macro to avoid asm()
 * boilerplate. Drivers and most kernel code should use the TLB
 * management routines in preference to the macro below.
 *
 * The macro can be used as __tlbi(op) or __tlbi(op, arg), depending
 * on whether a particular TLBI operation takes an argument or
 * not. The macros handles invoking the asm with or without the
 * register argument as appropriate.
 */
/*
 * CONFIG_ARM64_WORKAROUND_REPEAT_TLBI 说明使用一个折中的方法来修复处
 * 理器中的硬件故障: tlbi + dsb + tlbi
 */
#define __TLBI_0(op, arg) asm ("tlbi " #op "\n"				       \
		   ALTERNATIVE("nop\n			nop",		       \
			       "dsb ish\n		tlbi " #op,	       \
			       ARM64_WORKAROUND_REPEAT_TLBI,		       \
			       CONFIG_ARM64_WORKAROUND_REPEAT_TLBI)	       \
			    : : )

#define __TLBI_1(op, arg) asm ("tlbi " #op ", %0\n"			       \
		   ALTERNATIVE("nop\n			nop",		       \
			       "dsb ish\n		tlbi " #op ", %0",     \
			       ARM64_WORKAROUND_REPEAT_TLBI,		       \
			       CONFIG_ARM64_WORKAROUND_REPEAT_TLBI)	       \
			    : : "r" (arg))

#define __TLBI_N(op, arg, n, ...) __TLBI_##n(op, arg)

#define __tlbi(op, ...)		__TLBI_N(op, ##__VA_ARGS__, 1, 0)

#define __tlbi_user(op, arg) do {						\
	if (arm64_kernel_unmapped_at_el0())					\
		__tlbi(op, (arg) | USER_ASID_FLAG);				\
} while (0)

/* This macro creates a properly formatted VA operand for the TLBI */
#define __TLBI_VADDR(addr, asid)				\
	({							\
		unsigned long __ta = (addr) >> 12;		\
		__ta &= GENMASK_ULL(43, 0);			\
		__ta |= (unsigned long)(asid) << 48;		\
		__ta;						\
	})

/*
 *	TLB Invalidation
 *	================
 *
 * 	This header file implements the low-level TLB invalidation routines
 *	(sometimes referred to as "flushing" in the kernel) for arm64.
 *
 *	Every invalidation operation uses the following template:
 *
 *	DSB ISHST	// Ensure prior page-table updates have completed
 *	TLBI ...	// Invalidate the TLB
 *	DSB ISH		// Ensure the TLB invalidation has completed
 *      if (invalidated kernel mappings)
 *		ISB	// Discard any instructions fetched from the old mapping
 *
 *
 *	The following functions form part of the "core" TLB invalidation API,
 *	as documented in Documentation/core-api/cachetlb.rst:
 *
 *	flush_tlb_all()
 *		Invalidate the entire TLB (kernel + user) on all CPUs
 *
 *	flush_tlb_mm(mm)
 *		Invalidate an entire user address space on all CPUs.
 *		The 'mm' argument identifies the ASID to invalidate.
 *
 *	flush_tlb_range(vma, start, end)
 *		Invalidate the virtual-address range '[start, end)' on all
 *		CPUs for the user address space corresponding to 'vma->mm'.
 *		Note that this operation also invalidates any walk-cache
 *		entries associated with translations for the specified address
 *		range.
 *
 *	flush_tlb_kernel_range(start, end)
 *		Same as flush_tlb_range(..., start, end), but applies to
 * 		kernel mappings rather than a particular user address space.
 *		Whilst not explicitly documented, this function is used when
 *		unmapping pages from vmalloc/io space.
 *
 *	flush_tlb_page(vma, addr)
 *		Invalidate a single user mapping for address 'addr' in the
 *		address space corresponding to 'vma->mm'.  Note that this
 *		operation only invalidates a single, last-level page-table
 *		entry and therefore does not affect any walk-caches.
 *
 *
 *	Next, we have some undocumented invalidation routines that you probably
 *	don't want to call unless you know what you're doing:
 *
 *	local_flush_tlb_all()
 *		Same as flush_tlb_all(), but only applies to the calling CPU.
 *
 *	__flush_tlb_kernel_pgtable(addr)
 *		Invalidate a single kernel mapping for address 'addr' on all
 *		CPUs, ensuring that any walk-cache entries associated with the
 *		translation are also invalidated.
 *
 *	__flush_tlb_range(vma, start, end, stride, last_level)
 *		Invalidate the virtual-address range '[start, end)' on all
 *		CPUs for the user address space corresponding to 'vma->mm'.
 *		The invalidation operations are issued at a granularity
 *		determined by 'stride' and only affect any walk-cache entries
 *		if 'last_level' is equal to false.
 *
 *
 *	Finally, take a look at asm/tlb.h to see how tlb_flush() is implemented
 *	on top of these routines, since that is our interface to the mmu_gather
 *	API as used by munmap() and friends.
 */
/*
 * 使本地 CPU(当前核)对应的所有 TLB 表项无效。
 * 和函数 flush_tlb_all 的区别如下：
 *   1.指令 dsb 中的字段 ish 换成了 nsh ， nsh 是非共享(non-shareable)，表示数据
 *     同步屏障指令仅仅在当前核起作用。
 *   2.指令 tlbi 没有携带字段 is ，表示仅仅使当前核的 TLB 表项失效。
 */
static inline void local_flush_tlb_all(void)
{
	dsb(nshst);
	__tlbi(vmalle1);
	dsb(nsh);
	isb();
}

/* 使所有处理器上的所有 TLB 表项(包括内核空间和用户空间的 TLB)无效 */
static inline void flush_tlb_all(void)
{
    /* 确保屏障前面的存储指令执行完，如修改页表等操作 */
	dsb(ishst);
    /*
     * vmalle1is 使 EL1 中所有 VMID 指定的 TLB 无效，这里仅仅指的是内部
     * 共享的 TLB
     */
	__tlbi(vmalle1is);
    /* 保证前面的 TLBI 指令执行完成 */
	dsb(ish);
    /*
     * 冲刷处理器的流水线，重新读取屏障指令后面的所有指令。如在流水线中丢弃已经
     * 从旧页表映射中获取的指令
     */
	isb();
}

/* 使指定进程地址空间的所有 TLB 表项无效 */
static inline void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long asid = __TLBI_VADDR(0, ASID(mm));

	dsb(ishst);
	__tlbi(aside1is, asid);
	__tlbi_user(aside1is, asid);
	dsb(ish);
}

static inline void flush_tlb_page_nosync(struct vm_area_struct *vma,
					 unsigned long uaddr)
{
	unsigned long addr = __TLBI_VADDR(uaddr, ASID(vma->vm_mm));

	dsb(ishst);
	__tlbi(vale1is, addr);
	__tlbi_user(vale1is, addr);
}

/* 使指定进程虚拟地址空间的虚拟地址 addr 所映射页面的 TLB 页表项无效 */
static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long uaddr)
{
	flush_tlb_page_nosync(vma, uaddr);
	dsb(ish);
}

/*
 * This is meant to avoid soft lock-ups on large TLB flushing ranges and not
 * necessarily a performance improvement.
 */
#define MAX_TLBI_OPS	PTRS_PER_PTE

static inline void __flush_tlb_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end,
				     unsigned long stride, bool last_level)
{
    /* 获取当前进程对应的 ASID */
	unsigned long asid = ASID(vma->vm_mm);
	unsigned long addr;

	if ((end - start) >= (MAX_TLBI_OPS * stride)) {
		flush_tlb_mm(vma->vm_mm);
		return;
	}

	/* Convert the stride into units of 4k */
	stride >>= 12;

    /* 生成 TLBI 指令所需参数 */
	start = __TLBI_VADDR(start, asid);
	end = __TLBI_VADDR(end, asid);

	dsb(ishst);
	for (addr = start; addr < end; addr += stride) {
		if (last_level) {
            /*
             * vale1is 使 EL1 中所有由虚拟地址指定的 TLB 无效，这里指的是
             * 内部共享的 TLB
             */
			__tlbi(vale1is, addr);
			__tlbi_user(vale1is, addr);
		} else {
			__tlbi(vae1is, addr);
			__tlbi_user(vae1is, addr);
		}
	}
	dsb(ish);
}

/* 使指定进程地址空间的某段虚拟地址区间(从 start 到 end)对应的 TLB 表项无效 */
static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	/*
	 * We cannot use leaf-only invalidation here, since we may be invalidating
	 * table entries as part of collapsing hugepages or moving page tables.
	 */
	__flush_tlb_range(vma, start, end, PAGE_SIZE, false);
}

/* 使内核地址空间的某段虚拟地址区间(从 start 到 end)对应的 TLB 表项无效 */
static inline void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long addr;

	if ((end - start) > (MAX_TLBI_OPS * PAGE_SIZE)) {
		flush_tlb_all();
		return;
	}

	start = __TLBI_VADDR(start, 0);
	end = __TLBI_VADDR(end, 0);

	dsb(ishst);
	for (addr = start; addr < end; addr += 1 << (PAGE_SHIFT - 12))
		__tlbi(vaale1is, addr);
	dsb(ish);
	isb();
}

/*
 * Used to invalidate the TLB (walk caches) corresponding to intermediate page
 * table levels (pgd/pud/pmd).
 */
static inline void __flush_tlb_kernel_pgtable(unsigned long kaddr)
{
	unsigned long addr = __TLBI_VADDR(kaddr, 0);

	dsb(ishst);
	__tlbi(vaae1is, addr);
	dsb(ish);
}
#endif

#endif
