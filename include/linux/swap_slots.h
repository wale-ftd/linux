/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAP_SLOTS_H
#define _LINUX_SWAP_SLOTS_H

#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

#define SWAP_SLOTS_CACHE_SIZE			SWAP_BATCH
#define THRESHOLD_ACTIVATE_SWAP_SLOTS_CACHE	(5*SWAP_SLOTS_CACHE_SIZE)
#define THRESHOLD_DEACTIVATE_SWAP_SLOTS_CACHE	(2*SWAP_SLOTS_CACHE_SIZE)

/*
 * 匿名页 swapout 的时候需要从 swap area 中分配 swap slot ，如果频繁分配，有
 * 2 个影响性能的明显缺陷：
 *   1.频繁持锁，导致锁竞争
 *   2.Swap slot的碎片化
 * 为了提高 swap slot 的分配速度，内核引入了 swap slot cache 机制。
 *
 * 会定义了一个 per_cpu 的 swp_slots 全局变量(在 mm/swap_slots.c)
 */
struct swap_slots_cache {
	bool		lock_initialized;
	/* 下面 4 个变量用于分配，见 get_swap_page() */
	struct mutex	alloc_lock; /* protects slots, nr, cur */
	swp_entry_t	*slots;
	int		nr;
	int		cur;
	/* 下面 3 个变量用于释放，见 free_swap_slot() */
	spinlock_t	free_lock;  /* protects slots_ret, n_ret */
	swp_entry_t	*slots_ret;
	int		n_ret;
};

void disable_swap_slots_cache_lock(void);
void reenable_swap_slots_cache_unlock(void);
int enable_swap_slots_cache(void);
int free_swap_slot(swp_entry_t entry);

extern bool swap_slot_cache_enabled;

#endif /* _LINUX_SWAP_SLOTS_H */
