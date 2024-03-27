/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_MMAN_COMMON_H
#define __ASM_GENERIC_MMAN_COMMON_H

/*
 Author: Michael S. Tsirkin <mst@mellanox.co.il>, Mellanox Technologies Ltd.
 Based on: asm-xxx/mman.h
*/

#define PROT_READ	0x1		/* page can be read */
#define PROT_WRITE	0x2		/* page can be written */
#define PROT_EXEC	0x4		/* page can be executed */
#define PROT_SEM	0x8		/* page may be used for atomic ops */
#define PROT_NONE	0x0		/* page can not be accessed */
#define PROT_GROWSDOWN	0x01000000	/* mprotect flag: extend change to start of growsdown vma */
#define PROT_GROWSUP	0x02000000	/* mprotect flag: extend change to end of growsup vma */

/*
 * 1. MAP_ANONYMOUS | MAP_PRIVATE: 私有匿名映射。如 glibc 分配大内存块(128KB
 *                                 以上)
 * 2. MAP_ANONYMOUS | MAP_SHARED: 共享匿名映射。如相关进程共享一块内存区域，
 *                                通常用于父子进程之间的通信
 * 3. MAP_PRIVATE: 私有文件映射。如加载动态共享库(为什么？因为加载动态共享库
 *                 会使用动态链接写 GOT 表， GOT 表里的数据每个进程可能不一样
 *                 ，这是进程的私有数据，不能共享)
 * 4. MAP_SHARED: 共享文件映射。如进程间(非父子进程)通信、读写文件(为什么？因
 *                为私有映射修改内容不会同步到磁盘)
 */

/*
 * 多个进程可以通过共享映射方式来映射一个文件，这样其他进程也可以看到映射内容
 * 的改变，修改后的内容会同步到磁盘文件中
 */
#define MAP_SHARED	0x01		/* Share changes */
/*
 * 创建一个私有的写时复制的映射。多个进程可以通过私有映射的方式来映射一个文件，
 * 这样其他进程不会看到映射内容的改变，修改后的内容也不会同步到磁盘中
 */
#define MAP_PRIVATE	0x02		/* Changes are private */
#define MAP_SHARED_VALIDATE 0x03	/* share + validate extension flags */
#define MAP_TYPE	0x0f		/* Mask for type of mapping */
/*
 * 如果 addr 和 length 指定的进程地址空间和已有的 VMA 重叠，那么内核会调用
 * do_munmap() 函数把这段重叠区域销毁，然后重新映射新的内容
 */
#define MAP_FIXED	0x10		/* Interpret addr exactly */
#define MAP_ANONYMOUS	0x20		/* don't use a file */
#ifdef CONFIG_MMAP_ALLOW_UNINITIALIZED
# define MAP_UNINITIALIZED 0x4000000	/* For anonymous mmap, memory could be uninitialized */
#else
# define MAP_UNINITIALIZED 0x0		/* Don't support this flag */
#endif

/* 0x0100 - 0x80000 flags are defined in asm-generic/mman.h */
#define MAP_FIXED_NOREPLACE	0x100000	/* MAP_FIXED which doesn't unmap underlying mapping */

/*
 * Flags for mlock
 */
#define MLOCK_ONFAULT	0x01		/* Lock pages in range after they are faulted in, do not prefault */

#define MS_ASYNC	1		/* sync memory asynchronously */
#define MS_INVALIDATE	2		/* invalidate the caches */
#define MS_SYNC		4		/* synchronous memory sync */

#define MADV_NORMAL	0		/* no further special treatment */
/* 随机读。会设置随机读标记，清除顺序读标记 */
#define MADV_RANDOM	1		/* expect random page references */
/* 预期按照顺序访问指定范围的页，所以可以激进地预读指定范围的页，并且进程在
 * 访问页以后很快释放。
 *
 * MADV_SEQUENTIAL + 增大预读窗口(VM_MAX_READHEAD/BLKRASET)更适合流媒体服务
 * 的场景
 */
#define MADV_SEQUENTIAL	2		/* expect sequential page references */
/*
 * 预期很快就会访问指定范围的页，所以可以预读指定范围的页。因为仅预读指定的
 * 长度，因此在读取新的文件区域时，要重新调用 MADV_WILLNEED 。所以不适合流媒
 * 体服务的场景，比较适合内核很难预测接下来要预读哪些内容的场景，如随机读。
 */
#define MADV_WILLNEED	3		/* will need these pages */
/*
 * 预期近期不会访问指定范围的页，即进程已经处理完指定范围的页，内核可以释放
 * 相关的资源
 */
#define MADV_DONTNEED	4		/* don't need these pages */

/* common parameters: try to keep these consistent across architectures */
#define MADV_FREE	8		/* free pages only if memory pressure */
#define MADV_REMOVE	9		/* remove these pages & resources */
#define MADV_DONTFORK	10		/* don't inherit across fork */
#define MADV_DOFORK	11		/* do inherit across fork */
#define MADV_HWPOISON	100		/* poison a page for testing */
/*
 * 使指定范围的页软下线，即内存页被保留，但是下一次访问的时候，把数据复制到
 * 新的物理页，旧的物理页下线，对进程不可见。这个特性用来测试处理内存错误的
 * 代码
 */
#define MADV_SOFT_OFFLINE 101		/* soft offline page for testing */

#define MADV_MERGEABLE   12		/* KSM may merge identical pages */
#define MADV_UNMERGEABLE 13		/* KSM may not merge identical pages */

#define MADV_HUGEPAGE	14		/* Worth backing with hugepages */
#define MADV_NOHUGEPAGE	15		/* Not worth backing with hugepages */

#define MADV_DONTDUMP   16		/* Explicity exclude from the core dump,
					   overrides the coredump filter bits */
#define MADV_DODUMP	17		/* Clear the MADV_DONTDUMP flag */

#define MADV_WIPEONFORK 18		/* Zero memory on fork, child only */
#define MADV_KEEPONFORK 19		/* Undo MADV_WIPEONFORK */

/* compatibility flags */
#define MAP_FILE	0

#define PKEY_DISABLE_ACCESS	0x1
#define PKEY_DISABLE_WRITE	0x2
#define PKEY_ACCESS_MASK	(PKEY_DISABLE_ACCESS |\
				 PKEY_DISABLE_WRITE)

#endif /* __ASM_GENERIC_MMAN_COMMON_H */
