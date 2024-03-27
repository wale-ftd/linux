/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SLAB_DEF_H
#define	_LINUX_SLAB_DEF_H

#include <linux/reciprocal_div.h>

/*
 * Definitions unique to the original Linux SLAB allocator.
 */

struct kmem_cache {
	/*
	 * 对象缓冲池。每个 CPU 都有一个，当前 CPU 的称为本地对象缓冲池。用来缓存刚
	 * 刚释放的对象，分配时首先从当前处理器的数组缓存分配，避免每次都要从 slab
	 * 分配，提高分配速度。有以下好处：
	 *   1.刚释放的对象很可能还在处理器的缓存中，可以更好地利用处理器的缓存
	 *   2.减少链表操作
	 *   3.避免处理器之间的互斥，减少自旋锁操作
	 *
	 * 分配对象的时候，先从当前处理器的数组缓存分配对象。如果数组缓存是空的，那
	 * 么批量分配对象以重新填充数组缓存，批量值就是数组缓存的成员 batchcount 。
	 * 释放对象的时候，如果数组缓存是满的，那么先把数组缓存中的对象批量归还给
	 * slab ，批量值就是数组缓存的成员 batchcount ，然后把正在释放的对象存放到数
	 * 组缓存中。
	 */
	struct array_cache __percpu *cpu_cache;

/* 1) Cache tunables. Protected by slab_mutex */
	/* 用于批量迁移对象的数目。 本地对象缓冲池 <-> 共享对象缓冲池/slabs_partial/slabs_free */
	unsigned int batchcount;
	unsigned int limit;
	/* == 8 */
	unsigned int shared;

	/*
	 * 对象布局如下：
	 *
	 * |<------------------------------ size ------------------------------>|
	 * |<--- 8 字节 --->|<- obj_size ->|      |<- 8 字节 ->|<--- 8 字节 --->|
	 * +----------------+--------------+------+------------+----------------+
	 * |   红色区域 1   |   真实对象   | 填充 | 红色区域 2 | 最后一个使用者 |
	 * +----------------+--------------+------+------------+----------------+
	 * |<- obj_offset ->|
	 *
	 * 其中，红色区域 1 、红色区域 2 、最后一个使用者是使能了 CONFIG_DEBUG_SLAB
	 * 时才有的。
	 * 红色区域 1 ：长度是 8 字节，写入一个魔幻数，如果值被修改，说明对象被改写。
	 * 填充：用来对齐的填充字节。
	 * 红色区域 2 ：长度是 8 字节，写入一个魔幻数，如果值被修改，说明对象被改写。
	 * 最后一个使用者：在 64 位系统上长度是 8 字节，存放最后一个调用者的地址，用
	 *                 来确定对象被谁改写
	 *
	 * CONFIG_DEBUG_SLAB 原理：
	 * 1.分配对象时，把对象毒化：把最后 1 字节以外的每个字节设置为 0x5a ，把最后
	 *   一个字节设置为 0xa5 ；把对象前后的红色区域设置为宏 RED_ACTIVE 表示的魔
	 *   幻数；字段"最后一个使用者"保存调用函数的地址。
	 * 2.释放对象时，检查对象：如果对象前后的红色区域都是宏 RED_ACTIVE 表示的魔
	 *   幻数，说明正常；如果对象前后的红色区域都是宏 RED_INACTIVE 表示的魔幻数，
	 *   说明重复释放；其他情况，说明写越界。
	 * 3.释放对象时，把对象毒化：把最后 1 字节以外的每个字节设置为 0x6b ，把最后
	 *   1 字节设置为 0xa5 ；把对象前后的红色区域都设置为 RED_INACTIVE ，字段"最
	 *   后一个使用者"保存调用函数的地址。
	 * 4.再次分配对象时，检查对象：如果对象不符合"最后 1 字节以外的每个字节是
	 *   0x6b ，最后 1 字节是 0xa5"，说明对象被改写；如果对象前后的红色区域不是
	 *   宏 RED_INACTIVE 表示的魔幻数，说明重复释放或者写越界。
	 *
	 * size 是是对象实际占用的内存长度，即 object_size + 填充的长度，填充主要是
	 * 为了提高访问对象的速度，需要把对象的地址和长度都对齐到某个值，对齐值的计
	 * 算步骤如下。
	 *   1.如果创建内存缓存时指定了标志位 SLAB_HWCACHE_ALIGN ，要求和处理器的一
	 *     级缓存行的长度对齐，计算对齐值的方法如下。
	 *       a.如果对象的长度大于一级缓存行的长度的一半，对齐值取一级缓存行的长
	 *         度。
	 *       b.如果对象的长度小于或等于一级缓存行的长度的一半，对齐值取(一级缓
	 *         存行的长度/2^n)，把 2^n 个对象放在一个一级缓存行里面，需要为 n
	 *         找到一个合适的值。
	 *       c.如果对齐值小于指定的对齐值，取指定的对齐值。
	 *   举例说明：假设指定的对齐值是 4 字节，一级缓存行的长度是 32 字节，对象的
	 *   长度是 12 字节，那么对齐值是 16 字节，对象占用的内存长度是 16 字节，把
	 *   两个对象放在一个一级缓存行里面。
	 *   2.如果对齐值小于 ARCH_SLAB_MINALIGN ，那么取 ARCH_SLAB_MINALIGN 。
	 *     ARCH_SLAB_MINALIGN 是各种处理器架构定义的最小对齐值，默认值是 8 。
	 *   3.把对齐值向上调整为指针长度的整数倍。
	 */
	unsigned int size;
	struct reciprocal_value reciprocal_buffer_size;
/* 2) touched by every alloc & free from the backend */

	/* 如 SLAB_HWCACHE_ALIGN */
	slab_flags_t flags;		/* constant flags */
	unsigned int num;		/* # of objs per slab */

/* 3) cache_grow/shrink */
	/* order of pgs per slab (2^n) */
	/* 如果阶数大于 0 ，组成一个复合页 */
	unsigned int gfporder;

	/* force GFP flags, e.g. GFP_DMA */
	gfp_t allocflags;

	/*
	 * slab 是一个或多个连续的物理页，起始地址总是页长度的整数倍，不同 slab 中相
	 * 同偏移的位置在处理器的一级缓存中的索引相同。如果 slab 的剩余部分的长度超
	 * 过一级缓存行的长度，剩余部分对应的一级缓存行没有被利用；如果对象的填充字
	 * 节的长度超过一级缓存行的长度，填充字节对应的一级缓存行没有被利用。这两种
	 * 情况导致处理器的某些缓存行被过度使用，另一些缓存行很少使用。
	 *
	 * 在 slab 的剩余部分的长度超过一级缓存行长度的情况下，为了均匀利用处理器的
	 * 所有一级缓存行， slab 着色(slab coloring)利用 slab 的剩余部分， 使不同
	 * slab 的第一个对象的偏移不同。
	 *
	 * 着色是一个比喻，和颜色无关，只是表示 slab 中的第一个对象需要移动一个偏移
	 * 值，使对象放到不同的一级缓存行里。
	 *
	 * slab 被划分为多个对象，大多数情况下 slab 长度不是对象长度的整数倍， slab
	 * 有剩余部分，可以用来给 slab 着色：把 slab 的第一个对象从 slab 的起始位置
	 * 偏移一个数值，偏移值是处理器的一级缓存行长度的整数倍，不同 slab 的偏移值
	 * 不同，使不同 slab 的对象映射到处理器不同的缓存行。所以我们看到在 slab 的
	 * 前面有一个着色部分。
	 */
	/*
	 * 着色范围，等于 (slab 的剩余长度/颜色偏移)。表示一个 slab 分配器中有多少个
	 * 不同的 colour_off(高速缓存行) 用于着色
	 */
	size_t colour;			/* cache colouring range */
	/* 等于处理器的一级缓存行的长度，如果小于对齐值，那么取对齐值 */
	unsigned int colour_off;	/* colour offset */
	/*
	 * 对于 OFF_SLAB 内存布局，对象管理区是通过 kmalloc(本质也是 kmem_cache)
	 * 来分配的，freelist_cache 指向 freelist_size 对应大小的 kmem_cache
	 */
	struct kmem_cache *freelist_cache;
	/* 对象管理区的大小 */
	unsigned int freelist_size;

	/* constructor func */
	void (*ctor)(void *obj);

/* 4) cache creation/removal */
	const char *name;
	struct list_head list;
	int refcount;
	/* 成员 object_size 是对象原始长度，成员 size 是包括填充的对象长度 */
	int object_size;
	int align;

/* 5) statistics */
#ifdef CONFIG_DEBUG_SLAB
	unsigned long num_active;
	unsigned long num_allocations;
	unsigned long high_mark;
	unsigned long grown;
	unsigned long reaped;
	unsigned long errors;
	unsigned long max_freeable;
	unsigned long node_allocs;
	unsigned long node_frees;
	unsigned long node_overflow;
	atomic_t allochit;
	atomic_t allocmiss;
	atomic_t freehit;
	atomic_t freemiss;
#ifdef CONFIG_DEBUG_SLAB_LEAK
	atomic_t store_user_clean;
#endif

	/*
	 * If debugging is enabled, then the allocator can add additional
	 * fields and/or padding to every object. 'size' contains the total
	 * object size including these internal fields, while 'obj_offset'
	 * and 'object_size' contain the offset to the user object and its
	 * size.
	 */
	int obj_offset;
#endif /* CONFIG_DEBUG_SLAB */

#ifdef CONFIG_MEMCG
	struct memcg_cache_params memcg_params;
#endif
#ifdef CONFIG_KASAN
	struct kasan_cache kasan_info;
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
	unsigned int *random_seq;
#endif

	unsigned int useroffset;	/* Usercopy region offset */
	unsigned int usersize;		/* Usercopy region size */

	/* 支持 NUMA 系统 */
	struct kmem_cache_node *node[MAX_NUMNODES];
};

static inline void *nearest_obj(struct kmem_cache *cache, struct page *page,
				void *x)
{
	void *object = x - (x - page->s_mem) % cache->size;
	void *last_object = page->s_mem + (cache->num - 1) * cache->size;

	if (unlikely(object > last_object))
		return last_object;
	else
		return object;
}

/*
 * We want to avoid an expensive divide : (offset / cache->size)
 *   Using the fact that size is a constant for a particular cache,
 *   we can replace (offset / cache->size) by
 *   reciprocal_divide(offset, cache->reciprocal_buffer_size)
 */
static inline unsigned int obj_to_index(const struct kmem_cache *cache,
					const struct page *page, void *obj)
{
	u32 offset = (obj - page->s_mem);
	return reciprocal_divide(offset, cache->reciprocal_buffer_size);
}

#endif	/* _LINUX_SLAB_DEF_H */
