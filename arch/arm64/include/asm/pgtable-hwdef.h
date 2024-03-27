/*
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
#ifndef __ASM_PGTABLE_HWDEF_H
#define __ASM_PGTABLE_HWDEF_H

#include <asm/memory.h>

/*
 * Number of page-table levels required to address 'va_bits' wide
 * address, without section mapping. We resolve the top (va_bits - PAGE_SHIFT)
 * bits with (PAGE_SHIFT - 3) bits at each page table level. Hence:
 *
 *  levels = DIV_ROUND_UP((va_bits - PAGE_SHIFT), (PAGE_SHIFT - 3))
 *
 * where DIV_ROUND_UP(n, d) => (((n) + (d) - 1) / (d))
 *
 * We cannot include linux/kernel.h which defines DIV_ROUND_UP here
 * due to build issues. So we open code DIV_ROUND_UP here:
 *
 *	((((va_bits) - PAGE_SHIFT) + (PAGE_SHIFT - 3) - 1) / (PAGE_SHIFT - 3))
 *
 * which gets simplified as :
 */
#define ARM64_HW_PGTABLE_LEVELS(va_bits) (((va_bits) - 4) / (PAGE_SHIFT - 3))

/*
 * Size mapped by an entry at level n ( 0 <= n <= 3)
 * We map (PAGE_SHIFT - 3) at all translation levels and PAGE_SHIFT bits
 * in the final page. The maximum number of translation levels supported by
 * the architecture is 4. Hence, starting at at level n, we have further
 * ((4 - n) - 1) levels of translation excluding the offset within the page.
 * So, the total number of bits mapped by an entry at level n is :
 *
 *  ((4 - n) - 1) * (PAGE_SHIFT - 3) + PAGE_SHIFT
 *
 * Rearranging it a bit we get :
 *   (4 - n) * (PAGE_SHIFT - 3) + 3
 */
/* == 21 */
#define ARM64_HW_PGTABLE_LEVEL_SHIFT(n)	((PAGE_SHIFT - 3) * (4 - (n)) + 3)

/* == 512 */
#define PTRS_PER_PTE		(1 << (PAGE_SHIFT - 3))

/*
 * PMD_SHIFT determines the size a level 2 page table entry can map.
 */
#if CONFIG_PGTABLE_LEVELS > 2
/* PMD_SHIFT = 21 */
#define PMD_SHIFT		ARM64_HW_PGTABLE_LEVEL_SHIFT(2)
#define PMD_SIZE		(_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE-1))
#define PTRS_PER_PMD		PTRS_PER_PTE
#endif

/*
 * PUD_SHIFT determines the size a level 1 page table entry can map.
 */
#if CONFIG_PGTABLE_LEVELS > 3
/* PUD_SHIFT = 30 */
#define PUD_SHIFT		ARM64_HW_PGTABLE_LEVEL_SHIFT(1)
#define PUD_SIZE		(_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK		(~(PUD_SIZE-1))
#define PTRS_PER_PUD		PTRS_PER_PTE
#endif

/*
 * PGDIR_SHIFT determines the size a top-level page table entry can map
 * (depending on the configuration, this level can be 0, 1 or 2).
 */
/* CONFIG_PGTABLE_LEVELS=4 ，所以 PGDIR_SHIFT = 39 */
#define PGDIR_SHIFT		ARM64_HW_PGTABLE_LEVEL_SHIFT(4 - CONFIG_PGTABLE_LEVELS)
#define PGDIR_SIZE		(_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))
/* 1 << 9 = 512 */
#define PTRS_PER_PGD		(1 << (MAX_USER_VA_BITS - PGDIR_SHIFT))

/*
 * Section address mask and size definitions.
 */
#define SECTION_SHIFT		PMD_SHIFT
#define SECTION_SIZE		(_AC(1, UL) << SECTION_SHIFT)
#define SECTION_MASK		(~(SECTION_SIZE-1))

/*
 * Contiguous page definitions.
 */
#ifdef CONFIG_ARM64_64K_PAGES
#define CONT_PTE_SHIFT		5
#define CONT_PMD_SHIFT		5
#elif defined(CONFIG_ARM64_16K_PAGES)
#define CONT_PTE_SHIFT		7
#define CONT_PMD_SHIFT		5
#else
/* 是这里 */
#define CONT_PTE_SHIFT		4
#define CONT_PMD_SHIFT		4
#endif

#define CONT_PTES		(1 << CONT_PTE_SHIFT)
#define CONT_PTE_SIZE		(CONT_PTES * PAGE_SIZE)
#define CONT_PTE_MASK		(~(CONT_PTE_SIZE - 1))
#define CONT_PMDS		(1 << CONT_PMD_SHIFT)
#define CONT_PMD_SIZE		(CONT_PMDS * PMD_SIZE)
#define CONT_PMD_MASK		(~(CONT_PMD_SIZE - 1))
/* the the numerical offset of the PTE within a range of CONT_PTES */
#define CONT_RANGE_OFFSET(addr) (((addr)>>PAGE_SHIFT)&(CONT_PTES-1))

/* Level 0 指 PGD */

/*
 * Hardware page table definitions.
 *
 * Level 1 descriptor (PUD).
 */
/*
 * PUD 页表项的 bit[1:0] 。
 * bit[0] 页表项是否有效
 * bit[1] 。当该位为 0 时，说明这是一个段映射，即巨页的 PUD 页表项；当该位为 1 时，
 * 说明它指向下一级页表(即 PT)。
 */
#define PUD_TYPE_TABLE		(_AT(pudval_t, 3) << 0)
#define PUD_TABLE_BIT		(_AT(pudval_t, 1) << 1)
#define PUD_TYPE_MASK		(_AT(pudval_t, 3) << 0)
#define PUD_TYPE_SECT		(_AT(pudval_t, 1) << 0)

/*
 * Level 2 descriptor (PMD).
 */
/*
 * PMD 页表项的 bit[1:0] 。
 * bit[0] 页表项是否有效
 * bit[1] 。当该位为 0 时，说明这是一个段映射，即巨页的 PMD 页表项；当该位为 1 时，
 * 说明它指向下一级页表(即 PT)。
 */
#define PMD_TYPE_MASK		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_FAULT		(_AT(pmdval_t, 0) << 0)
#define PMD_TYPE_TABLE		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_SECT		(_AT(pmdval_t, 1) << 0)
#define PMD_TABLE_BIT		(_AT(pmdval_t, 1) << 1)

/*
 * Section
 */
#define PMD_SECT_VALID		(_AT(pmdval_t, 1) << 0)
#define PMD_SECT_USER		(_AT(pmdval_t, 1) << 6)		/* AP[1] */
#define PMD_SECT_RDONLY		(_AT(pmdval_t, 1) << 7)		/* AP[2] */
#define PMD_SECT_S		(_AT(pmdval_t, 3) << 8)
#define PMD_SECT_AF		(_AT(pmdval_t, 1) << 10)
#define PMD_SECT_NG		(_AT(pmdval_t, 1) << 11)
#define PMD_SECT_CONT		(_AT(pmdval_t, 1) << 52)
#define PMD_SECT_PXN		(_AT(pmdval_t, 1) << 53)
#define PMD_SECT_UXN		(_AT(pmdval_t, 1) << 54)

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
/* 用法见 MT_NORMAL */
#define PMD_ATTRINDX(t)		(_AT(pmdval_t, (t)) << 2)
#define PMD_ATTRINDX_MASK	(_AT(pmdval_t, 7) << 2)

/*
 * Level 3 descriptor (PTE).
 */
#define PTE_TYPE_MASK		(_AT(pteval_t, 3) << 0)
/* 无效页表项 */
#define PTE_TYPE_FAULT		(_AT(pteval_t, 0) << 0)
#define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
/* 页表类型的页表项 */
#define PTE_TABLE_BIT		(_AT(pteval_t, 1) << 1)
/*
 * 允许通过用户权限访问该内存。
 * 1: 可以通过 EL0(用户权限)以及更高权限的异常等级访问该内存
 * 0: 不能通过 EL0 访问，但可以通过 EL1 访问
 */
#define PTE_USER		(_AT(pteval_t, 1) << 6)		/* AP[1] */
/* 1: 只读； 0: 可读可写 */
#define PTE_RDONLY		(_AT(pteval_t, 1) << 7)		/* AP[2] */
/* 内存共享属性。 00: 没有共享； 01: 保留； 10: 外部可共享； 11: 内部可共享 */
#define PTE_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
/*
 * 当 AF 为 0 时，当第一次访问页面所在的地址范围时，硬件会根据是否使能了硬件管理
 * 访问位(TCR_EL1 里的 HA 字段是否为 1 ，ARMv8.1引入)做处理：
 *   1. 若使能，硬件会自动设置 AF 访问位。
 *   2. 若未使能，硬件会产生一个访问位的 page fault 来通知软件，然后软件就可以设
 *      置访问标志位为 1 。
 * 当 AF 为 1 时，表示该页面已经被访问过。
 */
#define PTE_AF			(_AT(pteval_t, 1) << 10)	/* Access Flag */
/*
 * 用于 TLB 管理。TLB 的页表项分成全局的和进程特有的。当设置该位时表示这个页面对应
 * 的 TLB 页表项是进程特有的
 */
#define PTE_NG			(_AT(pteval_t, 1) << 11)	/* nG */
/*
 * 表示页面被修改过。要使能了 TCR_EL1 里的 HD 字段(即使能了硬件管理脏位(ARMv8.1引
 * 入))才有意义，否则被 PTE_WRITE 复用。
 * 当使能了 TCR_EL1 里的 HD 字段(即使能了硬件管理脏位(ARMv8.1引入))时，当处理器写
 * 一个只读的内存地址时，硬件会检查 DBM 字段，若该字段为 1 ，那么处理器会自动修改
 * AP[2]位为 0 ，使该页面具有可写权限。
 */
#define PTE_DBM			(_AT(pteval_t, 1) << 51)	/* Dirty Bit Management */
/* 表示当前页表项处在一个连续物理页面集合中，可使用单个 TLB 页表项进行优化 */
#define PTE_CONT		(_AT(pteval_t, 1) << 52)	/* Contiguous range */
/* 该内存不能在特权模式下执行 */
#define PTE_PXN			(_AT(pteval_t, 1) << 53)	/* Privileged XN */
/* 该内存不能在用户模式下执行。如内核里的映射要设置这位 */
#define PTE_UXN			(_AT(pteval_t, 1) << 54)	/* User XN */
#define PTE_HYP_XN		(_AT(pteval_t, 1) << 54)	/* HYP XN */
/* [58:55] 是预留位，软件可以利用这些位来实现某些特殊功能 */

/* (((1<<36) - 1) << 12) = [47:12]为 1 = 0x0000fffffffff000 */
#define PTE_ADDR_LOW		(((_AT(pteval_t, 1) << (48 - PAGE_SHIFT)) - 1) << PAGE_SHIFT)
#ifdef CONFIG_ARM64_PA_BITS_52
#define PTE_ADDR_HIGH		(_AT(pteval_t, 0xf) << 12)
#define PTE_ADDR_MASK		(PTE_ADDR_LOW | PTE_ADDR_HIGH)
#else
/* 是这个。为 0x0000fffffffff000 */
#define PTE_ADDR_MASK		PTE_ADDR_LOW
#endif

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
/* 用法见 MT_NORMAL */
#define PTE_ATTRINDX(t)		(_AT(pteval_t, (t)) << 2)
#define PTE_ATTRINDX_MASK	(_AT(pteval_t, 7) << 2)

/*
 * 2nd stage PTE definitions
 */
#define PTE_S2_RDONLY		(_AT(pteval_t, 1) << 6)   /* HAP[2:1] */
#define PTE_S2_RDWR		(_AT(pteval_t, 3) << 6)   /* HAP[2:1] */
#define PTE_S2_XN		(_AT(pteval_t, 2) << 53)  /* XN[1:0] */

#define PMD_S2_RDONLY		(_AT(pmdval_t, 1) << 6)   /* HAP[2:1] */
#define PMD_S2_RDWR		(_AT(pmdval_t, 3) << 6)   /* HAP[2:1] */
#define PMD_S2_XN		(_AT(pmdval_t, 2) << 53)  /* XN[1:0] */

#define PUD_S2_RDONLY		(_AT(pudval_t, 1) << 6)   /* HAP[2:1] */
#define PUD_S2_RDWR		(_AT(pudval_t, 3) << 6)   /* HAP[2:1] */
#define PUD_S2_XN		(_AT(pudval_t, 2) << 53)  /* XN[1:0] */

/*
 * Memory Attribute override for Stage-2 (MemAttr[3:0])
 */
#define PTE_S2_MEMATTR(t)	(_AT(pteval_t, (t)) << 2)
#define PTE_S2_MEMATTR_MASK	(_AT(pteval_t, 0xf) << 2)

/*
 * EL2/HYP PTE/PMD definitions
 */
#define PMD_HYP			PMD_SECT_USER
#define PTE_HYP			PTE_USER

/*
 * Highest possible physical address supported.
 */
/* == 48 */
#define PHYS_MASK_SHIFT		(CONFIG_ARM64_PA_BITS)
#define PHYS_MASK		((UL(1) << PHYS_MASK_SHIFT) - 1)

#define TTBR_CNP_BIT		(UL(1) << 0)

/*
 * TCR flags.
 */
#define TCR_T0SZ_OFFSET		0
#define TCR_T1SZ_OFFSET		16
#define TCR_T0SZ(x)		((UL(64) - (x)) << TCR_T0SZ_OFFSET)
#define TCR_T1SZ(x)		((UL(64) - (x)) << TCR_T1SZ_OFFSET)
#define TCR_TxSZ(x)		(TCR_T0SZ(x) | TCR_T1SZ(x))
#define TCR_TxSZ_WIDTH		6
#define TCR_T0SZ_MASK		(((UL(1) << TCR_TxSZ_WIDTH) - 1) << TCR_T0SZ_OFFSET)

#define TCR_EPD0_SHIFT		7
#define TCR_EPD0_MASK		(UL(1) << TCR_EPD0_SHIFT)
#define TCR_IRGN0_SHIFT		8
#define TCR_IRGN0_MASK		(UL(3) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_NC		(UL(0) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WBWA		(UL(1) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WT		(UL(2) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WBnWA		(UL(3) << TCR_IRGN0_SHIFT)

#define TCR_EPD1_SHIFT		23
#define TCR_EPD1_MASK		(UL(1) << TCR_EPD1_SHIFT)
#define TCR_IRGN1_SHIFT		24
#define TCR_IRGN1_MASK		(UL(3) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_NC		(UL(0) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_WBWA		(UL(1) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_WT		(UL(2) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_WBnWA		(UL(3) << TCR_IRGN1_SHIFT)

#define TCR_IRGN_NC		(TCR_IRGN0_NC | TCR_IRGN1_NC)
#define TCR_IRGN_WBWA		(TCR_IRGN0_WBWA | TCR_IRGN1_WBWA)
#define TCR_IRGN_WT		(TCR_IRGN0_WT | TCR_IRGN1_WT)
#define TCR_IRGN_WBnWA		(TCR_IRGN0_WBnWA | TCR_IRGN1_WBnWA)
#define TCR_IRGN_MASK		(TCR_IRGN0_MASK | TCR_IRGN1_MASK)


#define TCR_ORGN0_SHIFT		10
#define TCR_ORGN0_MASK		(UL(3) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_NC		(UL(0) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WBWA		(UL(1) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WT		(UL(2) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WBnWA		(UL(3) << TCR_ORGN0_SHIFT)

#define TCR_ORGN1_SHIFT		26
#define TCR_ORGN1_MASK		(UL(3) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_NC		(UL(0) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_WBWA		(UL(1) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_WT		(UL(2) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_WBnWA		(UL(3) << TCR_ORGN1_SHIFT)

#define TCR_ORGN_NC		(TCR_ORGN0_NC | TCR_ORGN1_NC)
#define TCR_ORGN_WBWA		(TCR_ORGN0_WBWA | TCR_ORGN1_WBWA)
#define TCR_ORGN_WT		(TCR_ORGN0_WT | TCR_ORGN1_WT)
#define TCR_ORGN_WBnWA		(TCR_ORGN0_WBnWA | TCR_ORGN1_WBnWA)
#define TCR_ORGN_MASK		(TCR_ORGN0_MASK | TCR_ORGN1_MASK)

#define TCR_SH0_SHIFT		12
#define TCR_SH0_MASK		(UL(3) << TCR_SH0_SHIFT)
#define TCR_SH0_INNER		(UL(3) << TCR_SH0_SHIFT)

#define TCR_SH1_SHIFT		28
#define TCR_SH1_MASK		(UL(3) << TCR_SH1_SHIFT)
#define TCR_SH1_INNER		(UL(3) << TCR_SH1_SHIFT)
#define TCR_SHARED		(TCR_SH0_INNER | TCR_SH1_INNER)

#define TCR_TG0_SHIFT		14
#define TCR_TG0_MASK		(UL(3) << TCR_TG0_SHIFT)
#define TCR_TG0_4K		(UL(0) << TCR_TG0_SHIFT)
#define TCR_TG0_64K		(UL(1) << TCR_TG0_SHIFT)
#define TCR_TG0_16K		(UL(2) << TCR_TG0_SHIFT)

#define TCR_TG1_SHIFT		30
#define TCR_TG1_MASK		(UL(3) << TCR_TG1_SHIFT)
#define TCR_TG1_16K		(UL(1) << TCR_TG1_SHIFT)
#define TCR_TG1_4K		(UL(2) << TCR_TG1_SHIFT)
#define TCR_TG1_64K		(UL(3) << TCR_TG1_SHIFT)

#define TCR_IPS_SHIFT		32
#define TCR_IPS_MASK		(UL(7) << TCR_IPS_SHIFT)
#define TCR_A1			(UL(1) << 22)
#define TCR_ASID16		(UL(1) << 36)
#define TCR_TBI0		(UL(1) << 37)
#define TCR_TBI1		(UL(1) << 38)
#define TCR_HA			(UL(1) << 39)
#define TCR_HD			(UL(1) << 40)
#define TCR_NFD1		(UL(1) << 54)

/*
 * TTBR.
 */
#ifdef CONFIG_ARM64_PA_BITS_52
/*
 * This should be GENMASK_ULL(47, 2).
 * TTBR_ELx[1] is RES0 in this configuration.
 */
#define TTBR_BADDR_MASK_52	(((UL(1) << 46) - 1) << 2)
#endif

#ifdef CONFIG_ARM64_USER_VA_BITS_52
/* Must be at least 64-byte aligned to prevent corruption of the TTBR */
#define TTBR1_BADDR_4852_OFFSET	(((UL(1) << (52 - PGDIR_SHIFT)) - \
				 (UL(1) << (48 - PGDIR_SHIFT))) * 8)
#endif

#endif
