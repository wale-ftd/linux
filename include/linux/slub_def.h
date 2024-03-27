/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SLUB_DEF_H
#define _LINUX_SLUB_DEF_H

/*
 * SLUB : A Slab allocator without object queues.
 *
 * (C) 2007 SGI, Christoph Lameter
 */
#include <linux/kobject.h>

enum stat_item {
	ALLOC_FASTPATH,		/* Allocation from cpu slab */
	ALLOC_SLOWPATH,		/* Allocation by getting a new cpu slab */
	FREE_FASTPATH,		/* Free to cpu slab */
	FREE_SLOWPATH,		/* Freeing not to cpu slab */
	FREE_FROZEN,		/* Freeing to frozen slab */
	FREE_ADD_PARTIAL,	/* Freeing moves slab to partial list */
	FREE_REMOVE_PARTIAL,	/* Freeing removes last object */
	ALLOC_FROM_PARTIAL,	/* Cpu slab acquired from node partial list */
	ALLOC_SLAB,		/* Cpu slab acquired from page allocator */
	ALLOC_REFILL,		/* Refill cpu slab from slab freelist */
	ALLOC_NODE_MISMATCH,	/* Switching cpu slab */
	FREE_SLAB,		/* Slab freed to the page allocator */
	CPUSLAB_FLUSH,		/* Abandoning of the cpu slab */
	DEACTIVATE_FULL,	/* Cpu slab was full when deactivated */
	DEACTIVATE_EMPTY,	/* Cpu slab was empty when deactivated */
	DEACTIVATE_TO_HEAD,	/* Cpu slab was moved to the head of partials */
	DEACTIVATE_TO_TAIL,	/* Cpu slab was moved to the tail of partials */
	DEACTIVATE_REMOTE_FREES,/* Slab contained remotely freed objects */
	DEACTIVATE_BYPASS,	/* Implicit deactivation */
	ORDER_FALLBACK,		/* Number of times fallback was necessary */
	CMPXCHG_DOUBLE_CPU_FAIL,/* Failure of this_cpu_cmpxchg_double */
	CMPXCHG_DOUBLE_FAIL,	/* Number of times that cmpxchg double did not match */
	CPU_PARTIAL_ALLOC,	/* Used cpu partial on alloc */
	CPU_PARTIAL_FREE,	/* Refill cpu partial on free */
	CPU_PARTIAL_NODE,	/* Refill cpu partial from node partial */
	CPU_PARTIAL_DRAIN,	/* Drain cpu partial to node partial */
	NR_SLUB_STAT_ITEMS };

struct kmem_cache_cpu {
	/* 指向当前使用的 slab 的空闲对象链表 */
	void **freelist;	/* Pointer to next available object */
	unsigned long tid;	/* Globally unique transaction id */
	/*
	 * 指向当前使用的 slab 对应的 page 实例。其中 page.frozen 的值为 1 ，表示当
	 * 前 slab 被冻结在每处理器 slab 缓存中； page.freelist 被设置为空指针
	 */
	struct page *page;	/* The slab from which we are allocating */
#ifdef CONFIG_SLUB_CPU_PARTIAL
	/*
	 * 指向每处理器部分空闲 slab 链表，第一个 slab 的 page.next 指向下一个 slab ，
	 * 从而组成一个单链表
	 */
	struct page *partial;	/* Partially allocated frozen slabs */
#endif
#ifdef CONFIG_SLUB_STATS
	unsigned stat[NR_SLUB_STAT_ITEMS];
#endif
};

#ifdef CONFIG_SLUB_CPU_PARTIAL
#define slub_percpu_partial(c)		((c)->partial)

#define slub_set_percpu_partial(c, p)		\
({						\
	slub_percpu_partial(c) = (p)->next;	\
})

#define slub_percpu_partial_read_once(c)     READ_ONCE(slub_percpu_partial(c))
#else
#define slub_percpu_partial(c)			NULL

#define slub_set_percpu_partial(c, p)

#define slub_percpu_partial_read_once(c)	NULL
#endif // CONFIG_SLUB_CPU_PARTIAL

/*
 * Word size structure that can be atomically updated or read and that
 * contains both the order and the number of objects that a slab of the
 * given order would contain.
 */
struct kmem_cache_order_objects {
	unsigned int x;
};

/*
 * Slab cache management.
 */
/*
 * SLUB 分配器继承了 SLAB 分配器的核心思想，在某些地方做了改进。
 *   1.SLAB 分配器的链表多，分为空闲 slab 链表、部分空闲 slab 链表和满 slab 链表，
 *     管理复杂。 SLUB 分配器只保留部分空闲 slab 链表。
 *   2.SLAB 分配器对 NUMA 系统的支持复杂，每个内存节点有共享数组缓存和远程节点数
 *     组缓存，对象在这些数组缓存之间转移，实现复杂。 SLUB 分配器做了简化。
 *   3.SLUB 分配器抛弃了效果不明显的 slab 着色。
 */
struct kmem_cache {
	/*
	 * per cpu slab 缓存。 SLAB 分配器的每处理器数组缓存以对象为单位，而 SLUB 分
	 * 配器的每处理器 slab 缓存以 slab 为单位
	 *
	 * 分配对象时，首先从当前处理器的 slab 缓存分配，如果当前有一个 slab 正在使
	 * 用并且有空闲对象，那么分配一个对象；如果 slab 缓存中的部分空闲 slab 链表
	 * 不是空的，那么取第一个 slab 作为当前使用的 slab ；其他情况下，需要重填当
	 * 前处理器的 slab 缓存。
	 *   1.如果内存节点的部分空闲 slab 链表不是空的，那么取第一个 slab 作为当前
	 *     使用的 slab ，并且重填 slab 缓存中的部分空闲 slab 链表，直到取出的所
	 *     有 slab 的空闲对象总数超过限制 kmem_cache.cpu_partial 的一半为止。
	 *   2.否则，创建一个新的 slab ，作为当前使用的 slab 。
	 *
	 * 什么情况下会把 slab 放到每处理器部分空闲 slab 链表中？
	 * 释放对象的时候，如果对象所属的 slab 以前没有空闲对象，并且没有冻结在每处
	 * 理器 slab 缓存中，那么把 slab 放到当前处理器的部分空闲 slab 链表中。如果
	 * 发现当前处理器的部分空闲 slab 链表中空闲对象的总数超过限制
	 * kmem_cache.cpu_partial ，先把链表中的所有 slab 归还到内存节点的部分空闲
	 * slab 链表中。
	 * 这种做法的好处是：把空闲对象非常少的 slab 放在每处理器空闲 slab 链表中，
	 * 优先从空闲对象非常少的 slab 分配对象，减少内存浪费。
	 */
	struct kmem_cache_cpu __percpu *cpu_slab;
	/* Used for retriving partial slabs etc */
	slab_flags_t flags;
	/* 最小部分空闲 slab 数量，值为 log(对象长度)/2 ，但会限制在范围[5,10] */
	unsigned long min_partial;
	unsigned int size;	/* The size of an object including meta data */
	unsigned int object_size;/* The size of an object without meta data */
	unsigned int offset;	/* Free pointer offset. */
#ifdef CONFIG_SLUB_CPU_PARTIAL
	/* Number of per cpu partial objects to keep around */
	/* 决定了链表中空闲对象的最大数量，是根据对象长度估算的值 */
	unsigned int cpu_partial;
#endif
	/*
	 * 每个 slab 由一个或多个连续的物理页组成，页的阶数是最优 slab 或最小 slab
	 * 的阶数，如果阶数大于 0 ，组成一个复合页
	 */
	/*
	 * 存放最优 slab 的阶数和对象数，低 16 位是对象数，高 16 位是 slab 的阶数，
	 * 即 oo 等于((slab 的阶数 << 16) | 对象数)。最优 slab 是剩余部分最小的 slab
	 */
	struct kmem_cache_order_objects oo;

	/* Allocation and freeing of slabs */
	struct kmem_cache_order_objects max;
	/*
	 * 存放最小 slab 的阶数和对象数，格式和 oo 相同。最小 slab 只需要足够存放一
	 * 个对象。当设备长时间运行以后，内存碎片化，分配连续物理页很难成功，如果分
	 * 配最优 slab 失败，就分配最小 slab
	 */
	struct kmem_cache_order_objects min;
	gfp_t allocflags;	/* gfp flags to use on each alloc */
	int refcount;		/* Refcount for slab cache destroy */
	void (*ctor)(void *);
	unsigned int inuse;		/* Offset to metadata */
	unsigned int align;		/* Alignment */
	unsigned int red_left_pad;	/* Left redzone padding size */
	const char *name;	/* Name (only for display!) */
	struct list_head list;	/* List of slab caches */
#ifdef CONFIG_SYSFS
	struct kobject kobj;	/* For sysfs */
	struct work_struct kobj_remove_work;
#endif
#ifdef CONFIG_MEMCG
	struct memcg_cache_params memcg_params;
	/* for propagation, maximum size of a stored attr */
	unsigned int max_attr_size;
#ifdef CONFIG_SYSFS
	struct kset *memcg_kset;
#endif
#endif

#ifdef CONFIG_SLAB_FREELIST_HARDENED
	unsigned long random;
#endif

#ifdef CONFIG_NUMA
	/*
	 * Defragmentation by allocating from a remote node.
	 */
	/*
	 * 用来控制从远程节点借用部分空闲 slab 和从本地节点取部分空闲 slab 的比例，
	 * 值越小，从本地节点取部分空闲 slab 的倾向越大。默认值是 1000 ，可以通过文
	 * 件 /sys/kernel/slab/<内存缓存名称>/remote_node_defrag_ratio 设置某个内存
	 * 缓存的远程节点反碎片比例，用户设置的范围是[0, 100]，内存缓存保存的比例值
	 * 是乘以 10 以后的值。
	 */
	unsigned int remote_node_defrag_ratio;
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
	unsigned int *random_seq;
#endif

#ifdef CONFIG_KASAN
	struct kasan_cache kasan_info;
#endif

	unsigned int useroffset;	/* Usercopy region offset */
	unsigned int usersize;		/* Usercopy region size */

	/*
	 * 支持 NUMA 系统
	 *
	 * 分配对象时，如果当前处理器的 slab 缓存是空的，需要重填当前处理器的 slab
	 * 缓存。 首先从本地内存节点的部分空闲 slab 链表中取 slab ， 如果本地内存节
	 * 点的部分空闲 slab 链表是空的，那么从其他内存节点的部分空闲 slab 链表借用
	 * slab 。
	 */
	struct kmem_cache_node *node[MAX_NUMNODES];
};

#ifdef CONFIG_SLUB_CPU_PARTIAL
#define slub_cpu_partial(s)		((s)->cpu_partial)
#define slub_set_cpu_partial(s, n)		\
({						\
	slub_cpu_partial(s) = (n);		\
})
#else
#define slub_cpu_partial(s)		(0)
#define slub_set_cpu_partial(s, n)
#endif // CONFIG_SLUB_CPU_PARTIAL

#ifdef CONFIG_SYSFS
#define SLAB_SUPPORTS_SYSFS
void sysfs_slab_unlink(struct kmem_cache *);
void sysfs_slab_release(struct kmem_cache *);
#else
static inline void sysfs_slab_unlink(struct kmem_cache *s)
{
}
static inline void sysfs_slab_release(struct kmem_cache *s)
{
}
#endif

void object_err(struct kmem_cache *s, struct page *page,
		u8 *object, char *reason);

void *fixup_red_left(struct kmem_cache *s, void *p);

static inline void *nearest_obj(struct kmem_cache *cache, struct page *page,
				void *x) {
	void *object = x - (x - page_address(page)) % cache->size;
	void *last_object = page_address(page) +
		(page->objects - 1) * cache->size;
	void *result = (unlikely(object > last_object)) ? last_object : object;

	result = fixup_red_left(cache, result);
	return result;
}

#endif /* _LINUX_SLUB_DEF_H */
