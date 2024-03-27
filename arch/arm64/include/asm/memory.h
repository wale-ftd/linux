/*
 * Based on arch/arm/include/asm/memory.h
 *
 * Copyright (C) 2000-2002 Russell King
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
 *
 * Note: this file should not be included by non-asm/.h files
 */
#ifndef __ASM_MEMORY_H
#define __ASM_MEMORY_H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>
#include <asm/bug.h>
#include <asm/page-def.h>
#include <asm/sizes.h>

/*
 * Size of the PCI I/O space. This must remain a power of two so that
 * IO_SPACE_LIMIT acts as a mask for the low bits of I/O addresses.
 */
#define PCI_IO_SIZE		SZ_16M

/*
 * VMEMMAP_SIZE - allows the whole linear region to be covered by
 *                a struct page array
 */
/* 1 << 35+6 = 2048GB */
#define VMEMMAP_SIZE (UL(1) << (VA_BITS - PAGE_SHIFT - 1 + STRUCT_PAGE_MAX_SHIFT))

/*
 * PAGE_OFFSET - the virtual address of the start of the linear map (top
 *		 (VA_BITS - 1))
 * KIMAGE_VADDR - the virtual address of the start of the kernel image
 * VA_BITS - the maximum number of bits for virtual addresses.
 * VA_START - the first kernel virtual address.
 */
/* == 48 */
#define VA_BITS			(CONFIG_ARM64_VA_BITS)
/* == 0xffff000000000000 */
#define VA_START		(UL(0xffffffffffffffff) - \
	(UL(1) << VA_BITS) + 1)
/*
 * 表示物理内存在内核空间里做线性映射的起始地址，其值为 0xffff800000000000 。
 * Linux 在初始化时会把物理内存全部做一次线性映射。
 */
#define PAGE_OFFSET		(UL(0xffffffffffffffff) - \
	(UL(1) << (VA_BITS - 1)) + 1)
/* 表示内核映像文件映射到内核空间的起始虚拟地址，其值为 0xffff000010000000 */
#define KIMAGE_VADDR		(MODULES_END)
#define BPF_JIT_REGION_START	(VA_START + KASAN_SHADOW_SIZE)
#define BPF_JIT_REGION_SIZE	(SZ_128M)
#define BPF_JIT_REGION_END	(BPF_JIT_REGION_START + BPF_JIT_REGION_SIZE)
/* == 0xffff000010000000 */
#define MODULES_END		(MODULES_VADDR + MODULES_VSIZE)
#define MODULES_VADDR		(BPF_JIT_REGION_END)
#define MODULES_VSIZE		(SZ_128M)
#define VMEMMAP_START		(PAGE_OFFSET - VMEMMAP_SIZE)
#define PCI_IO_END		(VMEMMAP_START - SZ_2M)
#define PCI_IO_START		(PCI_IO_END - PCI_IO_SIZE)
/* == 0xffff7dfffec00000 */
#define FIXADDR_TOP		(PCI_IO_START - SZ_2M)

#define KERNEL_START      _text
#define KERNEL_END        _end

#ifdef CONFIG_ARM64_USER_VA_BITS_52
#define MAX_USER_VA_BITS	52
#else
#define MAX_USER_VA_BITS	VA_BITS
#endif

/*
 * Generic and tag-based KASAN require 1/8th and 1/16th of the kernel virtual
 * address space for the shadow region respectively. They can bloat the stack
 * significantly, so double the (minimum) stack size when they are in use.
 */
#ifdef CONFIG_KASAN
#define KASAN_SHADOW_SIZE	(UL(1) << (VA_BITS - KASAN_SHADOW_SCALE_SHIFT))
#ifdef CONFIG_KASAN_EXTRA
#define KASAN_THREAD_SHIFT	2
#else
#define KASAN_THREAD_SHIFT	1
#endif /* CONFIG_KASAN_EXTRA */
#else
#define KASAN_SHADOW_SIZE	(0)
#define KASAN_THREAD_SHIFT	0
#endif

/* == 14 */
#define MIN_THREAD_SHIFT	(14 + KASAN_THREAD_SHIFT)

/*
 * VMAP'd stacks are allocated at page granularity, so we must ensure that such
 * stacks are a multiple of page size.
 */
#if defined(CONFIG_VMAP_STACK) && (MIN_THREAD_SHIFT < PAGE_SHIFT)
#define THREAD_SHIFT		PAGE_SHIFT
#else
/* 是这个 */
#define THREAD_SHIFT		MIN_THREAD_SHIFT
#endif

#if THREAD_SHIFT >= PAGE_SHIFT
#define THREAD_SIZE_ORDER	(THREAD_SHIFT - PAGE_SHIFT)
#endif

#define THREAD_SIZE		(UL(1) << THREAD_SHIFT)

/*
 * By aligning VMAP'd stacks to 2 * THREAD_SIZE, we can detect overflow by
 * checking sp & (1 << THREAD_SHIFT), which we can do cheaply in the entry
 * assembly.
 */
#ifdef CONFIG_VMAP_STACK
#define THREAD_ALIGN		(2 * THREAD_SIZE)
#else
#define THREAD_ALIGN		THREAD_SIZE
#endif

#define IRQ_STACK_SIZE		THREAD_SIZE

#define OVERFLOW_STACK_SIZE	SZ_4K

/*
 * Alignment of kernel segments (e.g. .text, .data).
 */
#if defined(CONFIG_DEBUG_ALIGN_RODATA)
/*
 *  4 KB granule:   1 level 2 entry
 * 16 KB granule: 128 level 3 entries, with contiguous bit
 * 64 KB granule:  32 level 3 entries, with contiguous bit
 */
#define SEGMENT_ALIGN			SZ_2M
#else
/*
 *  4 KB granule:  16 level 3 entries, with contiguous bit
 * 16 KB granule:   4 level 3 entries, without contiguous bit
 * 64 KB granule:   1 level 3 entry
 */
#define SEGMENT_ALIGN			SZ_64K
#endif

/*
 * Memory types available.
 */
/*
 * memory type 并没有直接存放在页表项中，而是存放在 MAIR_ELn 中，页表项中使用
 * 一个 3 位索引值(PTE_ATTRINDX())来查找 MAIR_ELn 。
 *
 * G: 表示 gathering(聚集)。聚集表示在同一个内存类型的区域中允许把多次访问内
 *    存的操作合并成一次总线传输。如果地址被标记为 nG ，那么必须按照程序里面
 *    的地址和长度访问。如果地址被标记为 G ，处理器可以把两个"写一个字节"的访
 *    问合并成一个"写两个字节"的访问，也可以把对相同内存位置的多个访问合并，例
 *    如读相同位置两次，处理器只需要读一次，为两条指令返回相同的结果。
 * R: 表示 re-ordering(指令重排)。该属性决定对相同设备的多个访问是否可以重新排
 *    序。如果地址被标记为"不重排序"，那么对同一个块的访问总是按照程序顺序执行。
 * E: 表示 early write acknowledgement(提前写应答)。往外部设备写数据时，处理
 *    器先把数据写入写缓冲区中，若使能了提前写应答，则数据到达写缓冲区时会发
 *    送写应答；若没有使能了提前写应答，则数据到达外设时才发送写应答。
 *
 * 系统在 __cpu_setup(arch/arm64/mm/proc.S)初始化 MAIR 和内存类型。
 */
#define MT_DEVICE_nGnRnE	0
#define MT_DEVICE_nGnRE		1
#define MT_DEVICE_GRE		2
/* NC(Non-Cacheable) 表示关闭高速缓存 */
#define MT_NORMAL_NC		3
/* 打开高速缓存，高速缓存为 write back 策略 */
#define MT_NORMAL		4
/* 打开高速缓存，高速缓存为 write through 策略 */
#define MT_NORMAL_WT		5

/*
 * Memory types for Stage-2 translation
 */
#define MT_S2_NORMAL		0xf
#define MT_S2_DEVICE_nGnRE	0x1

/*
 * Memory types for Stage-2 translation when ID_AA64MMFR2_EL1.FWB is 0001
 * Stage-2 enforces Normal-WB and Device-nGnRE
 */
#define MT_S2_FWB_NORMAL	6
#define MT_S2_FWB_DEVICE_nGnRE	1

#ifdef CONFIG_ARM64_4K_PAGES
#define IOREMAP_MAX_ORDER	(PUD_SHIFT)
#else
#define IOREMAP_MAX_ORDER	(PMD_SHIFT)
#endif

#ifndef __ASSEMBLY__

#include <linux/bitops.h>
#include <linux/mmdebug.h>

extern s64			memstart_addr;
/* PHYS_OFFSET - the physical address of the start of memory. */
#define PHYS_OFFSET		({ VM_BUG_ON(memstart_addr & 1); memstart_addr; })

/* the virtual base of the kernel image (minus TEXT_OFFSET) */
/* == 0xffff000010000000 。在 arch/arm64/kernel/head.S 定义和初始化 */
extern u64			kimage_vaddr;

/* the offset between the kernel virtual and physical mappings */
/*
 * 当系统刚初始化时，内核映像通过块映射的方式映射到 KIMAGE_VADDR +
 * TEXT_OFFSET 的虚拟地址上，因为 kimage_voffset 表示内核映像虚拟地址和物理
 * 地址之间的偏移量，其值为 0xfffeffffd0000000 。
 * 在 arch/arm64/mm/mmu.c 定义，在 arch/arm64/kernel/head.S 初始化
 */
extern u64			kimage_voffset;

static inline unsigned long kaslr_offset(void)
{
	return kimage_vaddr - KIMAGE_VADDR;
}

/* the actual size of a user virtual address */
/* == VA_BITS 。在 arch/arm64/mm/mmu.c 定义，在 arch/arm64/kernel/head.S 初始化 */
extern u64			vabits_user;

/*
 * Allow all memory at the discovery stage. We will clip it later.
 */
#define MIN_MEMBLOCK_ADDR	0
#define MAX_MEMBLOCK_ADDR	U64_MAX

/*
 * PFNs are used to describe any physical page; this means
 * PFN 0 == physical address 0.
 *
 * This is the PFN of the first RAM page in the kernel
 * direct-mapped view.  We assume this is the first page
 * of RAM in the mem_map as well.
 */
#define PHYS_PFN_OFFSET	(PHYS_OFFSET >> PAGE_SHIFT)

/*
 * When dealing with data aborts, watchpoints, or instruction traps we may end
 * up with a tagged userland pointer. Clear the tag to get a sane pointer to
 * pass on to access_ok(), for instance.
 */
#define untagged_addr(addr)	\
	((__typeof__(addr))sign_extend64((u64)(addr), 55))

#ifdef CONFIG_KASAN_SW_TAGS
#define __tag_shifted(tag)	((u64)(tag) << 56)
#define __tag_set(addr, tag)	(__typeof__(addr))( \
		((u64)(addr) & ~__tag_shifted(0xff)) | __tag_shifted(tag))
#define __tag_reset(addr)	untagged_addr(addr)
#define __tag_get(addr)		(__u8)((u64)(addr) >> 56)
#else
#define __tag_set(addr, tag)	(addr)
#define __tag_reset(addr)	(addr)
#define __tag_get(addr)		0
#endif

/*
 * Physical vs virtual RAM address space conversion.  These are
 * private definitions which should NOT be used outside memory.h
 * files.  Use virt_to_phys/phys_to_virt/__pa/__va instead.
 */


/*
 * The linear kernel range starts in the middle of the virtual adddress
 * space. Testing the top bit for the start of the region is a
 * sufficient check.
 */
/* 0xffff800000000000 开始是线性映射空间(即 lm 地址空间) */
#define __is_lm_address(addr)	(!!((addr) & BIT(VA_BITS - 1)))

#define __lm_to_phys(addr)	(((addr) & ~PAGE_OFFSET) + PHYS_OFFSET)
#define __kimg_to_phys(addr)	((addr) - kimage_voffset)

#define __virt_to_phys_nodebug(x) ({					\
	phys_addr_t __x = (phys_addr_t)(x);				\
	__is_lm_address(__x) ? __lm_to_phys(__x) :			\
			       __kimg_to_phys(__x);			\
})

#define __pa_symbol_nodebug(x)	__kimg_to_phys((phys_addr_t)(x))

#ifdef CONFIG_DEBUG_VIRTUAL
extern phys_addr_t __virt_to_phys(unsigned long x);
extern phys_addr_t __phys_addr_symbol(unsigned long x);
#else
#define __virt_to_phys(x)	__virt_to_phys_nodebug(x)
#define __phys_addr_symbol(x)	__pa_symbol_nodebug(x)
#endif

#define __phys_to_virt(x)	((unsigned long)((x) - PHYS_OFFSET) | PAGE_OFFSET)
#define __phys_to_kimg(x)	((unsigned long)((x) + kimage_voffset))

/*
 * Convert a page to/from a physical address
 */
#define page_to_phys(page)	(__pfn_to_phys(page_to_pfn(page)))
#define phys_to_page(phys)	(pfn_to_page(__phys_to_pfn(phys)))

/*
 * Note: Drivers should NOT use these.  They are the wrong
 * translation for translating DMA addresses.  Use the driver
 * DMA support - see dma-mapping.h.
 */
#define virt_to_phys virt_to_phys
static inline phys_addr_t virt_to_phys(const volatile void *x)
{
	return __virt_to_phys((unsigned long)(x));
}

#define phys_to_virt phys_to_virt
static inline void *phys_to_virt(phys_addr_t x)
{
	return (void *)(__phys_to_virt(x));
}

/*
 * Drivers should NOT use these either.
 */
/*
 * __pa_symbol()和 __pa()的作用都是把内核虚拟地址转换为物理地址。两者之间是有
 * 区别的：内核中常常用 __pa_symbol()访问一些内核符号表的物理地址，如内核页表
 * swapper_pg_dir 、代码段的符号 _text 或者 __init_begin 等，这些内核符号表的
 * 链接地址在 vmalloc 区域，即从 KIMAGE_VADDR + TEXT_OFFSET 开始的虚拟地址，
 * 它和内核空间的线性映射区是不相同的。 __pa()可以通过虚拟地址来确定区域。在
 * 内存线性映射完成之前，不能直接通过 __pa()这个宏直接从线性映射地址转换到物
 * 理地址。
 * 用法可以参考 paging_init()。
 */
#define __pa(x)			__virt_to_phys((unsigned long)(x))
#define __pa_symbol(x)		__phys_addr_symbol(RELOC_HIDE((unsigned long)(x), 0))
#define __pa_nodebug(x)		__virt_to_phys_nodebug((unsigned long)(x))
#define __va(x)			((void *)__phys_to_virt((phys_addr_t)(x)))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)
#define virt_to_pfn(x)      __phys_to_pfn(__virt_to_phys((unsigned long)(x)))
#define sym_to_pfn(x)	    __phys_to_pfn(__pa_symbol(x))

/*
 *  virt_to_page(k)	convert a _valid_ virtual address to struct page *
 *  virt_addr_valid(k)	indicates whether a virtual address is valid
 */
#define ARCH_PFN_OFFSET		((unsigned long)PHYS_PFN_OFFSET)

#ifndef CONFIG_SPARSEMEM_VMEMMAP
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define _virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)
#else
#define __virt_to_pgoff(kaddr)	(((u64)(kaddr) & ~PAGE_OFFSET) / PAGE_SIZE * sizeof(struct page))
#define __page_to_voff(kaddr)	(((u64)(kaddr) & ~VMEMMAP_START) * PAGE_SIZE / sizeof(struct page))

#define page_to_virt(page)	({					\
	unsigned long __addr =						\
		((__page_to_voff(page)) | PAGE_OFFSET);			\
	__addr = __tag_set(__addr, page_kasan_tag(page));		\
	((void *)__addr);						\
})

#define virt_to_page(vaddr)	((struct page *)((__virt_to_pgoff(vaddr)) | VMEMMAP_START))

#define _virt_addr_valid(kaddr)	pfn_valid((((u64)(kaddr) & ~PAGE_OFFSET) \
					   + PHYS_OFFSET) >> PAGE_SHIFT)
#endif
#endif

#define _virt_addr_is_linear(kaddr)	\
	(__tag_reset((u64)(kaddr)) >= PAGE_OFFSET)
#define virt_addr_valid(kaddr)		\
	(_virt_addr_is_linear(kaddr) && _virt_addr_valid(kaddr))

/*
 * Given that the GIC architecture permits ITS implementations that can only be
 * configured with a LPI table address once, GICv3 systems with many CPUs may
 * end up reserving a lot of different regions after a kexec for their LPI
 * tables (one per CPU), as we are forced to reuse the same memory after kexec
 * (and thus reserve it persistently with EFI beforehand)
 */
#if defined(CONFIG_EFI) && defined(CONFIG_ARM_GIC_V3_ITS)
# define INIT_MEMBLOCK_RESERVED_REGIONS	(INIT_MEMBLOCK_REGIONS + NR_CPUS + 1)
#endif

#include <asm-generic/memory_model.h>

#endif
