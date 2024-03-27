/*
 * Macros for manipulating and testing flags related to a
 * pageblock_nr_pages number of pages.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Original author, Mel Gorman
 * Major cleanups and reduction of bit operations, Andy Whitcroft
 */
#ifndef PAGEBLOCK_FLAGS_H
#define PAGEBLOCK_FLAGS_H

#include <linux/types.h>

#define PB_migratetype_bits 3
/* Bit indices that affect a whole block of pages */
/* set_pageblock_flags_group()/get_pageblock_flags_group()管理迁移类型 */
enum pageblock_bits {
	PB_migrate,
	PB_migrate_end = PB_migrate + PB_migratetype_bits - 1,
			/* 3 bits required for migrate types */
    /*
     * 以下情况下会设置：
     *   1. 在隔离空闲页面 isolate_freepages 时，当整个 pageblock 都被扫描完
     *      了，即开始隔离的起始点 isolate_start_pfn 等于 pageblock 的结束点，
     *      那么当前的 pageblock 会被设置成 SKIP 状态。因为下一次扫描时大概率
     *      还是会找不到合适的页面，不如直接跳过。
     *   2. 在快速隔离 freepages 中 fast_isolate_around()，如果拓展扫描了整个
     *      pageblock ，但是隔离出来的空闲页面 nr_feepages 还是小于待迁移页面
     *      的数量 nr_migratepages ，也是会将该 pageblock 设置成 SKIP 状态，
     *      花了半天的功夫没隔离出几个空闲的页面，徒劳无功。
     *   3. 在快速隔离迁移页面 fast_find_migrateblock()时，如果待扫描的起始页
     *      面的页帧号大于(free scanner 的初始扫描值 free_pfn -
     *      migrate scanner 的初始扫描值 migrate_pfn)的 1/2 或者 1/8 ，也会将
     *      该 pageblock 设置成 SKIP 的状态。
     */
	PB_migrate_skip,/* If set the block is skipped by compaction */

	/*
	 * Assume the bits will always align on a word. If this assumption
	 * changes then get/set pageblock needs updating.
	 */
	/* == 4 */
	NR_PAGEBLOCK_BITS
};

#ifdef CONFIG_HUGETLB_PAGE

#ifdef CONFIG_HUGETLB_PAGE_SIZE_VARIABLE

/* Huge page sizes are variable */
extern unsigned int pageblock_order;

#else /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

/* Huge pages are a constant size */
#define pageblock_order		HUGETLB_PAGE_ORDER

#endif /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

#else /* CONFIG_HUGETLB_PAGE */

/* If huge pages are not used, group by MAX_ORDER_NR_PAGES */
#define pageblock_order		(MAX_ORDER-1)

#endif /* CONFIG_HUGETLB_PAGE */

#define pageblock_nr_pages	(1UL << pageblock_order)

/* Forward declaration */
struct page;

unsigned long get_pfnblock_flags_mask(struct page *page,
				unsigned long pfn,
				unsigned long end_bitidx,
				unsigned long mask);

void set_pfnblock_flags_mask(struct page *page,
				unsigned long flags,
				unsigned long pfn,
				unsigned long end_bitidx,
				unsigned long mask);

/* Declarations for getting and setting flags. See mm/page_alloc.c */
#define get_pageblock_flags_group(page, start_bitidx, end_bitidx) \
	get_pfnblock_flags_mask(page, page_to_pfn(page),		\
			end_bitidx,					\
			(1 << (end_bitidx - start_bitidx + 1)) - 1)
#define set_pageblock_flags_group(page, flags, start_bitidx, end_bitidx) \
	set_pfnblock_flags_mask(page, flags, page_to_pfn(page),		\
			end_bitidx,					\
			(1 << (end_bitidx - start_bitidx + 1)) - 1)

#ifdef CONFIG_COMPACTION
#define get_pageblock_skip(page) \
			get_pageblock_flags_group(page, PB_migrate_skip,     \
							PB_migrate_skip)
#define clear_pageblock_skip(page) \
			set_pageblock_flags_group(page, 0, PB_migrate_skip,  \
							PB_migrate_skip)
#define set_pageblock_skip(page) \
			set_pageblock_flags_group(page, 1, PB_migrate_skip,  \
							PB_migrate_skip)
#else
static inline bool get_pageblock_skip(struct page *page)
{
	return false;
}
static inline void clear_pageblock_skip(struct page *page)
{
}
static inline void set_pageblock_skip(struct page *page)
{
}
#endif /* CONFIG_COMPACTION */

#endif	/* PAGEBLOCK_FLAGS_H */
