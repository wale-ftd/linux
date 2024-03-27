/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/mm_types_task.h>

#include <linux/auxvec.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/uprobes.h>
#include <linux/page-flags-layout.h>
#include <linux/workqueue.h>

#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

typedef int vm_fault_t;

struct address_space;
struct mem_cgroup;
struct hmm;

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 *
 * If you allocate the page using alloc_pages(), you can use some of the
 * space in struct page for your own purposes.  The five words in the main
 * union are available, except for bit 0 of the first word which must be
 * kept clear.  Many users use this word to store a pointer to an object
 * which is guaranteed to be aligned.  If you use the same storage as
 * page->mapping, you must restore it to NULL before freeing the page.
 *
 * If your page will not be mapped to userspace, you can also use the four
 * bytes in the mapcount union, but you must call page_mapcount_reset()
 * before freeing it.
 *
 * If you want to use the refcount field, it must be used in such a way
 * that other CPUs temporarily incrementing and then decrementing the
 * refcount does not cause problems.  On receiving the page from
 * alloc_pages(), the refcount will be positive.
 *
 * If you allocate pages of order > 0, you can use some of the fields
 * in each subpage, but you may need to restore some of their values
 * afterwards.
 *
 * SLUB uses cmpxchg_double() to atomically update its freelist and
 * counters.  That requires that freelist & counters be adjacent and
 * double-word aligned.  We align all struct pages to double-word
 * boundaries, and ensure that 'freelist' is aligned within the
 * struct.
 */
#ifdef CONFIG_HAVE_ALIGNED_STRUCT_PAGE
#define _struct_page_alignment	__aligned(2 * sizeof(unsigned long))
#else
#define _struct_page_alignment
#endif

/*
 * 1. 物理地址 <-> PFN 用 __phys_to_pfn()/__pfn_to_phys()
 * 2. PFN <-> page 用 __pfn_to_page()/__page_to_pfn()
 * 3. 页表项的值(pgd_t/pud_t/pmd_t/pte_t) -> page 用 pgd_page()/pud_page()/
 *                                                   pmd_page()/pte_page()
 */
struct page {
	/*
	 * 布局见 SECTIONS_PGOFF 上面的注释。
	 *
	 * 用于 slob 时，设置标志位 PG_slab ，表示页属于 SLOB 分配器；设置标志位
	 * PG_slob_free ，表示 slab 在 slab 链表中。
	 */
	unsigned long flags;		/* Atomic flags, some possibly
					 * updated asynchronously */
	/*
	 * Five words (20/40 bytes) are available in this union.
	 * WARNING: bit 0 of the first word is used for PageTail(). That
	 * means the other users of this union MUST NOT use the bit to
	 * avoid collision and false-positive PageTail().
	 */
	union {
		struct {	/* Page cache and anonymous pages */
			/**
			 * @lru: Pageout list, eg. active_list protected by
			 * zone_lru_lock.  Sometimes used as a generic list
			 * by the page owner.
			 */
			/*
			 * 用于 slab 时，作为链表节点加入其中一条 slab 链表
			 * 用于 slub 时，作为链表节点加入部分空闲 slab 链表
			 * 用于 slob 时，作为链表节点加入 slab 链表
			 */
			struct list_head lru;
			/* See page-flags.h for PAGE_MAPPING_FLAGS */
            /*
             * 指向 struct address_space 或者 struct anon_vma 。
             * 常用 page_mapping()/page_rmapping()/PageAnon()/__PageMovable()/
             * PageKsm()
             */
			struct address_space *mapping;
            /*
             * __page_set_anon_rmap()/shmem_add_to_page_cache()/__add_to_page_cache_locked()
             * 如果是文件映射，表示从文件开始的第 index 个文件面
             *
             * 对于 PCP 中的页，存放 migratetype 。如果释放的页面的 migratetype
             * 大于 MIGRATE_PCPTYPES ，会先入 MIGRATE_MOVABLE 中，但其真实的迁移
             * 类型保存在 index 中，见 free_unref_page_commit()
             */
			pgoff_t index;		/* Our offset within mapping. */
			/**
			 * @private: Mapping-private opaque data.
			 * Usually used for buffer_heads if PagePrivate.
			 * Used for swp_entry_t if PageSwapCache.
			 * Indicates order in the buddy system if PageBuddy.
			 */
			/*
			 * 页面空闲时，存放其在伙伴系统中的 order 值
			 * 用于 zsmalloc 时，存放 struct zspage * ，见 create_page_chain()
			 * 用于 swap cache 时，存放 swp_entry_t ，见 add_to_swap_cache()
			 * 用于 buffer_head 时，存放 buffer_head * ，见 attach_page_buffers()
			 */
			unsigned long private;
		};
		struct {	/* slab, slob and slub */
			union {
				struct list_head slab_list;	/* uses lru */
				struct {	/* Partial pages */
					/*
					 * 用于 slub 时，指向下一个 slab 对应的 page 实例
					 */
					struct page *next;
					/*
					 * 用于 slub 时，第一个 slab 对应的 page 实例的成员 pages 存
					 * 放链表中 slab 的数量，成员 pobjects 存放链表中空闲对象的
					 * 数量；后面的 slab 没有使用这两个成员
					 */
#ifdef CONFIG_64BIT
					int pages;	/* Nr of pages left */
					int pobjects;	/* Approximate count */
#else
					short int pages;
					short int pobjects;
#endif
				};
			};
			/* 该 page 所属的 slab 。 free slab 对象的时候要用到 */
			struct kmem_cache *slab_cache; /* not slob */
			/* Double-word boundary */
			/*
			 * 用于 slab 时，指向对象管理区，是一个 unsigned short 类型的数组，
			 * 数组中元素存放是的对象索引。需要和 active 配合使用。
			 *
			 * 如果打开了 SLAB 空闲链表随机化的配置宏
			 * CONFIG_SLAB_FREELIST_RANDOM，数组中第 n 个元素存放的对象索引是随
			 * 机的
			 *
			 * 用于 slub/slob 时，指向第一个空闲对象
			 */
			void *freelist;		/* first free object */
			union {
				/* 存放 slab 第一个对象的地址 */
				void *s_mem;	/* slab: first object */
				unsigned long counters;		/* SLUB */
				struct {			/* SLUB */
					/* 表示已分配对象的数量 */
					unsigned inuse:16;
					/* 表示对象数量 */
					unsigned objects:15;
					/*
					 * 表示 slab 是否被冻结在每处理器 slab 缓存中。如果 slab 在
					 * 每处理器 slab 缓存中，它处于冻结状态；如果 slab 在内存节
					 * 点的部分空闲 slab 链表中，它处于解冻状态
					 */
					unsigned frozen:1;
				};
			};
		};
		/*
		 * 判断一个页是复合页的成员的方法是：页设置了标志位 PG_head(针对首页)，
		 * 或者页的成员 compound_head 的最低位是 1(针对尾页)，见 PageCompound()
		 */
		struct {	/* Tail pages of compound page */
			/*
			 * 所有尾页的成员 compound_head 存放首页的地址，并且把最低位设置为
			 * 1 。和成员 lru.pre 占用相同的位置
			 */
			unsigned long compound_head;	/* Bit zero is set */

			/* First tail page only */
			/* 以下三个成员对 page[1] 才有效 */
			/*
			 * 存放复合页释放函数数组的索引，如 COMPOUND_PAGE_DTOR 。和成员
			 * lru.next 占用相同的位置
			 */
			unsigned char compound_dtor;
			/* 存放复合页的阶数 n 。和成员 lru.next 占用相同的位置 */
			unsigned char compound_order;
			/*
			 * 表示复合页的映射计数(即多少个虚拟页映射到这个物理页)，初始值为
			 * -1 。这个成员和成员 mapping 组成一个联合体，占用相同的位置，其他
			 * 尾页把成员 mapping 设置为一个有毒的地址。
			 */
			atomic_t compound_mapcount;
		};
		struct {	/* Second tail page of compound page */
			unsigned long _compound_pad_1;	/* compound_head */
			unsigned long _compound_pad_2;
			struct list_head deferred_list;
		};
		struct {	/* Page table pages */
			unsigned long _pt_pad_1;	/* compound_head */
			pgtable_t pmd_huge_pte; /* protected by page->ptl */
			unsigned long _pt_pad_2;	/* mapping */
			union {
				struct mm_struct *pt_mm; /* x86 pgds only */
				atomic_t pt_frag_refcount; /* powerpc */
			};
#if ALLOC_SPLIT_PTLOCKS
			spinlock_t *ptl;
#else
			spinlock_t ptl;
#endif
		};
		struct {	/* ZONE_DEVICE pages */
			/** @pgmap: Points to the hosting device page map. */
			struct dev_pagemap *pgmap;
			unsigned long hmm_data;
			unsigned long _zd_pad_1;	/* uses mapping */
		};

		/** @rcu_head: You can use this to free a page by RCU. */
		struct rcu_head rcu_head;
	};

	union {		/* This union is 4 bytes in size. */
		/*
		 * If the page can be mapped to userspace, encodes the number
		 * of times this page is referenced by a page table.
		 */
		/*
		 * 初始值为 -1 (page_mapcount_reset()) 。只有一个进程(父进程)映射
		 * 时，为 0 。 page_mapped()
		 */
		atomic_t _mapcount;

		/*
		 * If the page is neither PageSlab nor mappable to userspace,
		 * the value stored here may help determine what this page
		 * is used for.  See page-flags.h for a list of page types
		 * which are currently stored here.
		 */
		unsigned int page_type;

		/*
		 * 一个 slab 可能由多个连续的 page 组成，active 表示当前 slab 中，
		 * 活跃对象(指已经被迁移到对象缓冲池的对象)的个数，也可以理解为待
		 * 迁移到对象缓冲池的起始空闲对象的下标。如果当前 slab 中没有活跃
		 * 对象，即全部是不活跃的空闲对象，那么这个 slab 在合适的时机会被
		 * 销毁
		 *
		 * 有两重意思。
		 * 如为 0 时，
		 *   1.表示已分配对象的数量为 0
		 *   2.下一次分配的对象是 page->s_mem + page->freelist[0]
		 * 如为 3 时，
		 *   1.表示已分配对象的数量为 3
		 *   2.下一次分配的对象是 page->s_mem + page->freelist[3]
		 */
		unsigned int active;		/* SLAB */
		/* 表示空闲单元的数量 */
		int units;			/* SLOB */
	};

	/* Usage count. *DO NOT USE DIRECTLY*. See page_ref.h */
    /* 初始值为 0 。建议使用 page_count()/get_page()/put_page()操作。 */
	atomic_t _refcount;

#ifdef CONFIG_MEMCG
	struct mem_cgroup *mem_cgroup;
#endif

	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
} _struct_page_alignment;

/*
 * Used for sizing the vmemmap region on some architectures
 */
/* 64B */
#define STRUCT_PAGE_MAX_SHIFT	(order_base_2(sizeof(struct page)))

#define PAGE_FRAG_CACHE_MAX_SIZE	__ALIGN_MASK(32768, ~PAGE_MASK)
#define PAGE_FRAG_CACHE_MAX_ORDER	get_order(PAGE_FRAG_CACHE_MAX_SIZE)

struct page_frag_cache {
	void * va;
#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	__u16 offset;
	__u16 size;
#else
	__u32 offset;
#endif
	/* we maintain a pagecount bias, so that we dont dirty cache line
	 * containing page->_refcount every time we allocate a fragment.
	 */
	unsigned int		pagecnt_bias;
	bool pfmemalloc;
};

typedef unsigned long vm_flags_t;

/*
 * A region containing a mapping of a non-memory backed file under NOMMU
 * conditions.  These are held in a global tree and are pinned by the VMAs that
 * map parts of them.
 */
struct vm_region {
	struct rb_node	vm_rb;		/* link in global region tree */
	vm_flags_t	vm_flags;	/* VMA vm_flags */
	unsigned long	vm_start;	/* start address of region */
	unsigned long	vm_end;		/* region initialised to here */
	unsigned long	vm_top;		/* region allocated to here */
	unsigned long	vm_pgoff;	/* the offset in vm_file corresponding to vm_start */
	struct file	*vm_file;	/* the backing file or NULL */

	int		vm_usage;	/* region usage count (access under nommu_region_sem) */
	bool		vm_icache_flushed : 1; /* true if the icache has been flushed for
						* this region */
};

#ifdef CONFIG_USERFAULTFD
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) { NULL, })
struct vm_userfaultfd_ctx {
	struct userfaultfd_ctx *ctx;
};
#else /* CONFIG_USERFAULTFD */
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) {})
struct vm_userfaultfd_ctx {};
#endif /* CONFIG_USERFAULTFD */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	/* The first cache line has the info for VMA tree walking. */

	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next, *vm_prev;

	struct rb_node vm_rb;

	/*
	 * Largest free memory gap in bytes to the left of this VMA.
	 * Either between this VMA and vma->vm_prev, or between one of the
	 * VMAs below us in the VMA rbtree and its ->vm_prev. This helps
	 * get_unmapped_area find a free area of the right size.
	 */
	unsigned long rb_subtree_gap;

	/* Second cache line starts here. */

	struct mm_struct *vm_mm;	/* The address space we belong to. */
    /* 在初始化 VMA 时设置此变量。在真正分配物理内存时(如 page_fault 时)设置到页表项中 */
	pgprot_t vm_page_prot;		/* Access permissions of this VMA. */
    /* VM_xxx ，如 VM_WRITE 。 vm_get_page_prot()将 vm_flags 转换成 page prot */
	unsigned long vm_flags;		/* Flags, see mm.h. */

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap interval tree.
	 */
	/*
	 * 为了支持查询一个文件区间被映射到哪些虚拟内存区域，把一个文件映射到的所有
	 * 虚拟内存区域加入该文件的地址空间结构体 address_space 的成员 i_mmap 指向
	 * 的区间树
	 */
	struct {
		struct rb_node rb;
		unsigned long rb_subtree_last;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	/* 链表头，元素为 avc->same_vma */
	struct list_head anon_vma_chain; /* Serialized by mmap_sem &
					  * page_table_lock */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	/* Function pointers to deal with this struct. */
    /* 如 generic_file_vm_ops/shmem_vm_ops/hugetlb_vm_ops/special_mapping_vmops */
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
    /*
     * 对于共享匿名映射: vma->vm_pgoff = 0 ，见 do_mmap()
     * 对于私有匿名映射:
     *   vma->vm_pgoff = addr >> PAGE_SHIFT ，见 do_brk_flags()/do_mmap()
     * 对于文件映射：表示文件内的偏移量。因为一个文件如果太大可能分散映射到不同的
     *               VMA 中，所以需要 vm_pgoff 来链接
     * 对于特殊映射:
     *   vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT ，见 insert_vm_struct()
     *
     * page->index - vm_pgoff 可以计算出页面在 VMA 里的偏移量，再加上 vma_start 就
     * 可以获得页面的虚拟地址，见 __vma_address()。
     * 对于匿名页面， __vma_address()的逆向操作是 linear_page_index()(计算 page->index)
     *
     * 通常使用 VM_PFNMAP ， vm_pgoff 可能指向第一个 PFN 映射，见 _vm_normal_page()
     */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units */
	struct file * vm_file;		/* File we map to (can be NULL). */
	void * vm_private_data;		/* was vm_pte (shared mem) */

	atomic_long_t swap_readahead_info;
#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
	struct vm_userfaultfd_ctx vm_userfaultfd_ctx;
} __randomize_layout;

struct core_thread {
	struct task_struct *task;
	struct core_thread *next;
};

struct core_state {
	atomic_t nr_threads;
	struct core_thread dumper;
	struct completion startup;
};

struct kioctx_table;
/*
 * ARM64 进程地址空间布局
 *
 * 0x0 -------------------------------------------> 0x0000ffffffffffff
 *  | 保留区 | 代码段 | 数据段 | 堆空间 | mmap 空间 | 栈 | arg | env |
 *  ------------------------------------------------------------------
 *
 * task_size   = 0x1000000000000,
 * env_end     =  0xffffd4013fed,
 * env_start   =  0xffffd4013fca,
 * arg_end     =  0xffffd4013fca,
 * arg_start   =  0xffffd4013fae,
 * start_stack =  0xffffd4012ec0,
 * mmap_base   =  0xffffbaa91000,
 * brk         =  0xaaaae834c000,
 * start_brk   =  0xaaaae81cd000,
 * end_data    =  0xaaaad3857490,
 * start_data  =  0xaaaad381ab80,
 * end_code    =  0xaaaad380a1c4,
 * start_code  =  0xaaaad36eb000,
 */
struct mm_struct {
	struct {
		struct vm_area_struct *mmap;		/* list of VMAs */
		struct rb_root mm_rb;
		u64 vmacache_seqnum;                   /* per-thread vmacache */
#ifdef CONFIG_MMU
        /*
         * 判断虚拟内存空间是否有足够的空间，返回一段没有映射过的空间的起始地址。
         * 在 arch_pick_mmap_layout()里设置，一般会使用具体的处理器架构的实现。
         * arm64 是 arch_get_unmapped_area_topdown
         */
		unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
#endif
		/* 在 arch_pick_mmap_layout()里设置，不同的虚拟内存布局，此值不一样 */
		unsigned long mmap_base;	/* base of mmap area */
		unsigned long mmap_legacy_base;	/* base of mmap area in bottom-up allocations */
#ifdef CONFIG_HAVE_ARCH_COMPAT_MMAP_BASES
		/* Base adresses for compatible mmap() */
		unsigned long mmap_compat_base;
		unsigned long mmap_compat_legacy_base;
#endif
        /* 进程空间大小 */
		unsigned long task_size;	/* size of task vm space */
		unsigned long highest_vm_end;	/* highest vma end address */
		pgd_t * pgd;

		/**
		 * @mm_users: The number of users including userspace.
		 *
		 * Use mmget()/mmget_not_zero()/mmput() to modify. When this
		 * drops to 0 (i.e. when the task exits and there are no other
		 * temporary reference holders), we also release a reference on
		 * @mm_count (which may then free the &struct mm_struct if
		 * @mm_count also drops to 0).
		 */
		/*
		 * 记录正在使用该进程地址空间的进程数目，如果两个用户线程共享该地址空间，
		 * 那么 mm_users 的值等于 2 。如果内核线程临时使用一个进程地址空间，
		 * mm_users 的值不会增加，还是为 1 。
		 */
		atomic_t mm_users;

		/**
		 * @mm_count: The number of references to &struct mm_struct
		 * (@mm_users count as 1).
		 *
		 * Use mmgrab()/mmdrop() to modify. When this drops to 0, the
		 * &struct mm_struct is freed.
		 */
		/*
		 * 如果两个用户线程共享该地址空间，那么 mm_count 的值等于 1 。如果内核线
		 * 程临时使用一个进程地址空间， mm_count 的值等于 2 。
		 */
		atomic_t mm_count;

#ifdef CONFIG_MMU
		atomic_long_t pgtables_bytes;	/* PTE page table pages */
#endif
		int map_count;			/* number of VMAs */

		spinlock_t page_table_lock; /* Protects page tables and some
					     * counters
					     */
		struct rw_semaphore mmap_sem;

		struct list_head mmlist; /* List of maybe swapped mm's.	These
					  * are globally strung together off
					  * init_mm.mmlist, and are protected
					  * by mmlist_lock
					  */


		unsigned long hiwater_rss; /* High-watermark of RSS usage */
		unsigned long hiwater_vm;  /* High-water virtual memory usage */

		unsigned long total_vm;	   /* Total pages mapped */
		unsigned long locked_vm;   /* Pages that have PG_mlocked set */
		unsigned long pinned_vm;   /* Refcount permanently increased */
		unsigned long data_vm;	   /* VM_WRITE & ~VM_SHARED & ~VM_STACK */
		unsigned long exec_vm;	   /* VM_EXEC & ~VM_WRITE & ~VM_STACK */
		unsigned long stack_vm;	   /* VM_STACK */
		/*
		 * 进程默认的虚拟内存标志是 VM_NOHUGEPAGE，即不使用透明巨型页；内核线程
		 * 默认的虚拟内存标志是 0
		 */
		unsigned long def_flags;

		spinlock_t arg_lock; /* protect the below fields */
		unsigned long start_code, end_code, start_data, end_data;
		unsigned long start_brk, brk, start_stack;
		unsigned long arg_start, arg_end, env_start, env_end;

		unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

		/*
		 * Special counters, in some configurations protected by the
		 * page_table_lock, in other configurations by being atomic.
		 */
		struct mm_rss_stat rss_stat;

		struct linux_binfmt *binfmt;

		/* Architecture-specific MM context */
		/* 处理器架构特定的内存管理上下文 */
		mm_context_t context;

		unsigned long flags; /* Must use atomic bitops to access */

		struct core_state *core_state; /* coredumping support */
#ifdef CONFIG_MEMBARRIER
		atomic_t membarrier_state;
#endif
#ifdef CONFIG_AIO
		spinlock_t			ioctx_lock;
		struct kioctx_table __rcu	*ioctx_table;
#endif
#ifdef CONFIG_MEMCG
		/*
		 * "owner" points to a task that is regarded as the canonical
		 * user/owner of this mm. All of the following must be true in
		 * order for it to be changed:
		 *
		 * current == mm->owner
		 * current->mm != mm
		 * new_owner->mm == mm
		 * new_owner->alloc_lock is held
		 */
		struct task_struct __rcu *owner;
#endif
		struct user_namespace *user_ns;

		/* store ref to file /proc/<pid>/exe symlink points to */
		struct file __rcu *exe_file;
#ifdef CONFIG_MMU_NOTIFIER
		struct mmu_notifier_mm *mmu_notifier_mm;
#endif
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
		pgtable_t pmd_huge_pte; /* protected by page_table_lock */
#endif
#ifdef CONFIG_NUMA_BALANCING
		/*
		 * numa_next_scan is the next time that the PTEs will be marked
		 * pte_numa. NUMA hinting faults will gather statistics and
		 * migrate pages to new nodes if necessary.
		 */
		unsigned long numa_next_scan;

		/* Restart point for scanning and setting pte_numa */
		unsigned long numa_scan_offset;

		/* numa_scan_seq prevents two threads setting pte_numa */
		int numa_scan_seq;
#endif
		/*
		 * An operation with batched TLB flushing is going on. Anything
		 * that can move process memory needs to flush the TLB when
		 * moving a PROT_NONE or PROT_NUMA mapped page.
		 */
		atomic_t tlb_flush_pending;
#ifdef CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH
		/* See flush_tlb_batched_pending() */
		bool tlb_flush_batched;
#endif
		struct uprobes_state uprobes_state;
#ifdef CONFIG_HUGETLB_PAGE
		atomic_long_t hugetlb_usage;
#endif
		struct work_struct async_put_work;

#if IS_ENABLED(CONFIG_HMM)
		/* HMM needs to track a few things per mm */
		struct hmm *hmm;
#endif
	} __randomize_layout;

	/*
	 * The mm_cpumask needs to be at the end of mm_struct, because it
	 * is dynamically sized based on nr_cpu_ids.
	 */
	unsigned long cpu_bitmap[];
};

extern struct mm_struct init_mm;

/* Pointer magic because the dynamic array size confuses some compilers. */
static inline void mm_init_cpumask(struct mm_struct *mm)
{
	unsigned long cpu_bitmap = (unsigned long)mm;

	cpu_bitmap += offsetof(struct mm_struct, cpu_bitmap);
	cpumask_clear((struct cpumask *)cpu_bitmap);
}

/* Future-safe accessor for struct mm_struct's cpu_vm_mask. */
static inline cpumask_t *mm_cpumask(struct mm_struct *mm)
{
	return (struct cpumask *)&mm->cpu_bitmap;
}

struct mmu_gather;
extern void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
				unsigned long start, unsigned long end);
extern void tlb_finish_mmu(struct mmu_gather *tlb,
				unsigned long start, unsigned long end);

static inline void init_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_set(&mm->tlb_flush_pending, 0);
}

static inline void inc_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_inc(&mm->tlb_flush_pending);
	/*
	 * The only time this value is relevant is when there are indeed pages
	 * to flush. And we'll only flush pages after changing them, which
	 * requires the PTL.
	 *
	 * So the ordering here is:
	 *
	 *	atomic_inc(&mm->tlb_flush_pending);
	 *	spin_lock(&ptl);
	 *	...
	 *	set_pte_at();
	 *	spin_unlock(&ptl);
	 *
	 *				spin_lock(&ptl)
	 *				mm_tlb_flush_pending();
	 *				....
	 *				spin_unlock(&ptl);
	 *
	 *	flush_tlb_range();
	 *	atomic_dec(&mm->tlb_flush_pending);
	 *
	 * Where the increment if constrained by the PTL unlock, it thus
	 * ensures that the increment is visible if the PTE modification is
	 * visible. After all, if there is no PTE modification, nobody cares
	 * about TLB flushes either.
	 *
	 * This very much relies on users (mm_tlb_flush_pending() and
	 * mm_tlb_flush_nested()) only caring about _specific_ PTEs (and
	 * therefore specific PTLs), because with SPLIT_PTE_PTLOCKS and RCpc
	 * locks (PPC) the unlock of one doesn't order against the lock of
	 * another PTL.
	 *
	 * The decrement is ordered by the flush_tlb_range(), such that
	 * mm_tlb_flush_pending() will not return false unless all flushes have
	 * completed.
	 */
}

static inline void dec_tlb_flush_pending(struct mm_struct *mm)
{
	/*
	 * See inc_tlb_flush_pending().
	 *
	 * This cannot be smp_mb__before_atomic() because smp_mb() simply does
	 * not order against TLB invalidate completion, which is what we need.
	 *
	 * Therefore we must rely on tlb_flush_*() to guarantee order.
	 */
	atomic_dec(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_pending(struct mm_struct *mm)
{
	/*
	 * Must be called after having acquired the PTL; orders against that
	 * PTLs release and therefore ensures that if we observe the modified
	 * PTE we must also observe the increment from inc_tlb_flush_pending().
	 *
	 * That is, it only guarantees to return true if there is a flush
	 * pending for _this_ PTL.
	 */
	return atomic_read(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_nested(struct mm_struct *mm)
{
	/*
	 * Similar to mm_tlb_flush_pending(), we must have acquired the PTL
	 * for which there is a TLB flush pending in order to guarantee
	 * we've seen both that PTE modification and the increment.
	 *
	 * (no requirement on actually still holding the PTL, that is irrelevant)
	 */
	return atomic_read(&mm->tlb_flush_pending) > 1;
}

struct vm_fault;

struct vm_special_mapping {
	const char *name;	/* The name, e.g. "[vdso]". */

	/*
	 * If .fault is not provided, this points to a
	 * NULL-terminated array of pages that back the special mapping.
	 *
	 * This must not be NULL unless .fault is provided.
	 */
	struct page **pages;

	/*
	 * If non-NULL, then this is called to resolve page faults
	 * on the special mapping.  If used, .pages is not checked.
	 */
	vm_fault_t (*fault)(const struct vm_special_mapping *sm,
				struct vm_area_struct *vma,
				struct vm_fault *vmf);

	int (*mremap)(const struct vm_special_mapping *sm,
		     struct vm_area_struct *new_vma);
};

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	TLB_REMOTE_SEND_IPI,
	NR_TLB_FLUSH_REASONS,
};

 /*
  * A swap entry has to fit into a "unsigned long", as the entry is hidden
  * in the "index" field of the swapper address space.
  */
typedef struct {
	unsigned long val;
} swp_entry_t;

#endif /* _LINUX_MM_TYPES_H */
