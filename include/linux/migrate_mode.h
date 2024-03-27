/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MIGRATE_MODE_H_INCLUDED
#define MIGRATE_MODE_H_INCLUDED
/*
 * MIGRATE_ASYNC means never block
 * MIGRATE_SYNC_LIGHT in the current implementation means to allow blocking
 *	on most operations but not ->writepage as the potential stall time
 *	is too significant
 * MIGRATE_SYNC will block when migrating pages
 * MIGRATE_SYNC_NO_COPY will block when migrating pages but will not copy pages
 *	with the CPU. Instead, page copy happens outside the migratepage()
 *	callback and is likely using a DMA engine. See migrate_vma() and HMM
 *	(mm/hmm.c) for users of this mode.
 */
/*
 * 异步模式是不能有 IO 的，即不允许有阻塞，同步模式支持有 IO 可以被阻塞。
 * 由于 MIGRATE_MOVABLE 和 MIGRATE_CMA 两种迁移类型不含有 IO 操作，故异步模式
 * 只能处理这两种。 MIGRATE_RECLAIMABLE 基本都是属于 file cache 类型的页面，
 * 在页面迁移过程中会有 IO 操作，所以不适用于异步模式。故同步模式可以处理三种
 * 种迁移类型的。
 */
enum migrate_mode {
    /*
     * 异步模式是禁止阻塞的，遇到阻塞时会直接返回，这种模式下不会对文件页进行
     * 处理，是一种比较轻盈的模式。
     * 应用场景：
     *   __alloc_pages_slowpath
     *     __alloc_pages_direct_compact(INIT_COMPACT_PRIORITY)
     *     if (costly_order && (gfp_mask & __GFP_NORETRY))
     *       compact_priority = INIT_COMPACT_PRIORITY;
     *     __alloc_pages_direct_compact(compact_priority)
     */
	MIGRATE_ASYNC,
	/*
	 * 轻同步模式下允许进行 MOVABLE, CMA, RECLAIMABLE 迁移类型的操作，此模式
	 * 下允许进行大部分的阻塞操作，但是不允许阻塞在脏页回写。
     * 应用场景：
     *   1.__alloc_pages_slowpath
     *       __alloc_pages_direct_compact(compact_priority)
     *   2.kcompactd
     *       kcompactd_do_work
	 */
	MIGRATE_SYNC_LIGHT,
	/*
	 * 同步模式是在轻同步模式的基础上，允许脏页的回写且此次 compaction 会等待
	 * 脏页回写结束。所以同步模式是最消耗系统资源的。
	 * 应用场景：
	 *   1.echo 1 > /proc/sys/vm/compact_memory
	 *     sysctl_compaction_handler -> compact_nodes
	 *   2.alloc_contig_range
	 *   3.NUMA 系统 node 间的页面迁移
     */
	MIGRATE_SYNC,
	/*
	 * 类似于同步模式，但是在迁移页面时 CPU 不会复制页面的内容，而是由 DMA 来
	 * 复制，如 HMM(见 mm/hmm.c)机制使用此模式
     */
	MIGRATE_SYNC_NO_COPY,
};

#endif		/* MIGRATE_MODE_H_INCLUDED */
