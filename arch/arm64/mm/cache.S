/*
 * Cache maintenance
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
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

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/asm-uaccess.h>

/*
 *	flush_icache_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(__flush_icache_range)
	/* FALLTHROUGH */

/*
 *	__flush_cache_user_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(__flush_cache_user_range)
    /*
     * uaccess_ttbr0_enable 宏用来使能在内核态访问用户地址空间。这里主要针对
     * 使用的软件来模拟特权模式禁止访问(PAN)功能，也就是使能了
     * CONFIG_ARM64_SW_TTBR0_PAN 宏。
     */
	uaccess_ttbr0_enable x2, x3, x4
/*
 * alternative_if 是一个宏，它的实现在 arch/arm64/include/asm/alternative.h 中
 *
 * ARM64_HAS_CACHE_IDC 表示以 PoU 做数据高速缓存管理时不需要对其做额外的清除
 * 操作，因此直接插入一个 DSB 指令即可完成数据高速缓存的清除操作。
 *
 * ARM64_HAS_CACHE_IDC 特性是在 arm64_features[] 数组中定义的，它的实现在
 * arch/arm64/kernel/cpufeature.c 中。
 */
alternative_if ARM64_HAS_CACHE_IDC
	dsb	ishst
	b	7f
alternative_else_nop_endif
    /*
     * 处理器不支持 ARM64_HAS_CACHE_IDC 时，必须调用 DC 指令来清除数据高速缓存。
     * dcache_line_size 宏用来获取高速缓存行的大小，并存放到 x2 寄存器中。
     */
	dcache_line_size x2, x3
	sub	x3, x2, #1
	/* 起始地址 start(x0) 按高速缓存行的大小对齐，对齐后的地址存放在 x4 中 */
	bic	x4, x0, x3
/*
 * 从起始地址 start 到结束地址 end ，按照高速缓存行依次调用 DC 指令来清除对应
 * 的高速缓存并使它失效。
 */
1:
/*
 * ARM64_WORKAROUND_CLEAN_CACHE(在 arch/arm64/kernel/cpu_errata.c 定义)表示
 * 这里有一个处理器硬件故障需要一些特殊处理，因此会调用 civac 辅助操作符(表
 * 示以 PoC 清除指定的虚拟地址对应的高速缓存并使它失效)来清除数据高速缓存。
 */
user_alt 9f, "dc cvau, x4",  "dc civac, x4",  ARM64_WORKAROUND_CLEAN_CACHE
	add	x4, x4, x2
	cmp	x4, x1
	b.lo	1b
	dsb	ish

7:
/*
 * ARM64_HAS_CACHE_DIC(在 arch/arm64/kernel/cpufeature.c 定义) 表示以 PoU 做
 * 指令高速缓存管理时不需要对其做额外的清除操作，因此直接插入一个 isb 指令即
 * 可完成指令高速缓存的清除工作。否则，就需要调用 invalidate_icache_by_line
 * 宏来清除指令高速缓存。
 */
alternative_if ARM64_HAS_CACHE_DIC
	isb
	b	8f
alternative_else_nop_endif
	invalidate_icache_by_line x0, x1, x2, x3, 9f
8:	mov	x0, #0
1:
    /* uaccess_ttbr0_disable 宏用来关闭在内核态访问用户地址空间 */
	uaccess_ttbr0_disable x1, x2
	ret
9:
	mov	x0, #-EFAULT
	b	1b
ENDPROC(__flush_icache_range)
ENDPROC(__flush_cache_user_range)

/*
 *	invalidate_icache_range(start,end)
 *
 *	Ensure that the I cache is invalid within specified region.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(invalidate_icache_range)
alternative_if ARM64_HAS_CACHE_DIC
	mov	x0, xzr
	isb
	ret
alternative_else_nop_endif

	uaccess_ttbr0_enable x2, x3, x4

	invalidate_icache_by_line x0, x1, x2, x3, 2f
	mov	x0, xzr
1:
	uaccess_ttbr0_disable x1, x2
	ret
2:
	mov	x0, #-EFAULT
	b	1b
ENDPROC(invalidate_icache_range)

/*
 *	__flush_dcache_area(kaddr, size)
 *
 *	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 *	are cleaned and invalidated to the PoC.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__flush_dcache_area)
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__flush_dcache_area)

/*
 *	__clean_dcache_area_pou(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoU.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__clean_dcache_area_pou)
alternative_if ARM64_HAS_CACHE_IDC
	dsb	ishst
	ret
alternative_else_nop_endif
	dcache_by_line_op cvau, ish, x0, x1, x2, x3
	ret
ENDPROC(__clean_dcache_area_pou)

/*
 *	__inval_dcache_area(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are invalidated. Any partial lines at the ends of the interval are
 *	also cleaned to PoC to prevent data loss.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__inval_dcache_area)
	/* FALLTHROUGH */

/*
 *	__dma_inv_area(start, size)
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
__dma_inv_area:
	add	x1, x1, x0
	dcache_line_size x2, x3
	sub	x3, x2, #1
	tst	x1, x3				// end cache line aligned?
	bic	x1, x1, x3
	b.eq	1f
	dc	civac, x1			// clean & invalidate D / U line
1:	tst	x0, x3				// start cache line aligned?
	bic	x0, x0, x3
	b.eq	2f
	dc	civac, x0			// clean & invalidate D / U line
	b	3f
2:	dc	ivac, x0			// invalidate D / U line
3:	add	x0, x0, x2
	cmp	x0, x1
	b.lo	2b
	dsb	sy
	ret
ENDPIPROC(__inval_dcache_area)
ENDPROC(__dma_inv_area)

/*
 *	__clean_dcache_area_poc(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoC.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__clean_dcache_area_poc)
	/* FALLTHROUGH */

/*
 *	__dma_clean_area(start, size)
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
__dma_clean_area:
	dcache_by_line_op cvac, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__clean_dcache_area_poc)
ENDPROC(__dma_clean_area)

/*
 *	__clean_dcache_area_pop(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoP.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__clean_dcache_area_pop)
	alternative_if_not ARM64_HAS_DCPOP
	b	__clean_dcache_area_poc
	alternative_else_nop_endif
	dcache_by_line_op cvap, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__clean_dcache_area_pop)

/*
 *	__dma_flush_area(start, size)
 *
 *	clean & invalidate D / U line
 *
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
ENTRY(__dma_flush_area)
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__dma_flush_area)

/*
 *	__dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(__dma_map_area)
	cmp	w2, #DMA_FROM_DEVICE
	b.eq	__dma_inv_area
	b	__dma_clean_area
ENDPIPROC(__dma_map_area)

/*
 *	__dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(__dma_unmap_area)
	cmp	w2, #DMA_TO_DEVICE
	b.ne	__dma_inv_area
	ret
ENDPIPROC(__dma_unmap_area)
