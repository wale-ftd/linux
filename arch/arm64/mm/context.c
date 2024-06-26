/*
 * Based on arch/arm/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
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

#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/cpufeature.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

/* 保存 ASID 长度 */
static u32 asid_bits;
static DEFINE_RAW_SPINLOCK(cpu_asid_lock);

/*
 * [63:asid_bis] 存放软件管理用的软件 generation 计数。当 asid generation 加 1
 * 时，每个处理器需要清空页表缓存
 */
static atomic64_t asid_generation;
/* 硬件 ASID 通过位图来管理，记录哪些 ASID 被分配 */
static unsigned long *asid_map;

/* 保存处理器正在使用的 ASID ，即处理器正在执行的进程的 ASID */
static DEFINE_PER_CPU(atomic64_t, active_asids);
/*
 * 存放保留的 ASID ，用来在 asid generation 加 1 时保存处理器正在执行的进程的
 * ASID 。
 *
 * 处理器给进程分配 ASID 时， 如果 ASID 分配完了，那么把 asid generation 加 1 ，
 * 重新从 1 开始分配 ASID ，针对每个处理器，使用该处理器的 reserved_asids 保存该
 * 处理器正在执行的进程的 ASID ， 并且把该处理器的 active_asids 设置为 0 。
 * active_asids 为 0 具有特殊含义，说明全局 asid generation 有变化， ASID 从最大
 * 值回绕到 1
 */
static DEFINE_PER_CPU(u64, reserved_asids);
/*
 * 保存需要清空页表缓存的处理器集合。当 asid generation 加 1 时，每个处理器需要
 * 清空页表缓存
 */
static cpumask_t tlb_flush_pending;

#define ASID_MASK		(~GENMASK(asid_bits - 1, 0))
#define ASID_FIRST_VERSION	(1UL << asid_bits)

/* 有定义 */
#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
/* == 32768 。使能 KPTI 后，每个进程要用两个 asid ，所以 asid 的总数会减半 */
#define NUM_USER_ASIDS		(ASID_FIRST_VERSION >> 1)
#define asid2idx(asid)		(((asid) & ~ASID_MASK) >> 1)
#define idx2asid(idx)		(((idx) << 1) & ~ASID_MASK)
#else
#define NUM_USER_ASIDS		(ASID_FIRST_VERSION)
#define asid2idx(asid)		((asid) & ~ASID_MASK)
#define idx2asid(idx)		asid2idx(idx)
#endif

/* Get the ASIDBits supported by the current CPU */
static u32 get_cpu_asid_bits(void)
{
	u32 asid;
	int fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64MMFR0_EL1),
						ID_AA64MMFR0_ASID_SHIFT);

	switch (fld) {
	default:
		pr_warn("CPU%d: Unknown ASID size (%d); assuming 8-bit\n",
					smp_processor_id(),  fld);
		/* Fallthrough */
	case 0:
		asid = 8;
		break;
	case 2:
		asid = 16;
	}

	return asid;
}

/* Check if the current cpu's ASIDBits is compatible with asid_bits */
void verify_cpu_asid_bits(void)
{
	u32 asid = get_cpu_asid_bits();

	if (asid < asid_bits) {
		/*
		 * We cannot decrease the ASID size at runtime, so panic if we support
		 * fewer ASID bits than the boot CPU.
		 */
		pr_crit("CPU%d: smaller ASID size(%u) than boot CPU (%u)\n",
				smp_processor_id(), asid, asid_bits);
		cpu_panic_kernel();
	}
}

/* 重新初始化 ASID 分配状态 */
static void flush_context(void)
{
	int i;
	u64 asid;

	/* Update the list of reserved ASIDs and the ASID bitmap. */
	/* 把 asid 位图清零 */
	bitmap_clear(asid_map, 0, NUM_USER_ASIDS);

	/*
	 * 把每个处理器的 active_asids 设置为 0 ， active_asids 为 0 具有特殊含义，
	 * 说明全局 ASID 版本号变化， ASID 回绕。 然后把每个处理器正在执行的进程的
	 * ASID 设置为保留 ASID ，为保留 ASID 在 ASID 位图中设置已分配的标志
	 */
	for_each_possible_cpu(i) {
		asid = atomic64_xchg_relaxed(&per_cpu(active_asids, i), 0);
		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * ASID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (asid == 0)
			asid = per_cpu(reserved_asids, i);
		__set_bit(asid2idx(asid), asid_map);
		per_cpu(reserved_asids, i) = asid;
	}

	/*
	 * Queue a TLB invalidation for each CPU to perform on next
	 * context-switch
	 */
	/*
	 * 所有处理器需要清空页表缓存，在位图 tlb_flush_pending 中设置所有处理器对应
	 * 的位
	 */
	cpumask_setall(&tlb_flush_pending);
}

static bool check_update_reserved_asid(u64 asid, u64 newasid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved ASIDs looking for a match.
	 * If we find one, then we can update our mm to use newasid
	 * (i.e. the same ASID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old ASID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved ASID in a future
	 * generation.
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_asids, cpu) == asid) {
			hit = true;
			per_cpu(reserved_asids, cpu) = newasid;
		}
	}

	return hit;
}

static u64 new_context(struct mm_struct *mm)
{
	static u32 cur_idx = 1;
	u64 asid = atomic64_read(&mm->context.id);
	u64 generation = atomic64_read(&asid_generation);

    /*
     * 刚创建进程时， mm->context.id 值初始化为 0 。如果这时 ASID 不为 0，说明该
     * 进程已经分配过 ASID
     */
	if (asid != 0) {
		u64 newasid = generation | (asid & ~ASID_MASK);

		/*
		 * If our current ASID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
        /*
         * 如果原来的 ASID 还有效(通过 check_update_reserved_asid()判断)，只需要
         * 更新 generation 即可组成一个新的软件 ASID 。
         */
		if (check_update_reserved_asid(asid, newasid))
			return newasid;

		/*
		 * We had a valid ASID in a previous life, so try to re-use
		 * it if possible.
		 */
		/*
		 * 如果旧 ASID 在位图中是空闲的，那么继续使用旧的 ASID，只需更新
		 * generation 即可组成一个新的软件 ASID 。
		 */
		if (!__test_and_set_bit(asid2idx(asid), asid_map))
			return newasid;
	}

	/*
	 * Allocate a free ASID. If we can't find one, take a note of the
	 * currently active ASIDs and mark the TLBs as requiring flushes.  We
	 * always count from ASID #2 (index 1), as we use ASID #0 when setting
	 * a reserved TTBR0 for the init_mm and we allocate ASIDs in even/odd
	 * pairs.
	 */
	/* 从上一次分配的 ASID 开始分配 ASID ，如果存在空闲的 ASID ，那么分配给进程 */
	asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, cur_idx);
	if (asid != NUM_USER_ASIDS)
		goto set_asid;

    /* 如果 ASID 已经分配完，那么提升 generation 值 */
	/* We're out of ASIDs, so increment the global generation count */
	generation = atomic64_add_return_relaxed(ASID_FIRST_VERSION,
						 &asid_generation);
    /* 重新初始化 ASID 分配状态，如把 asid_map 清零、刷新所有 CPU 上的 TLB */
	flush_context();

	/* We have more ASIDs than CPUs, so this will always succeed */
	/* 从 1 开始分配 ASID */
	asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, 1);

set_asid:
	/* 为刚分配的 ASID 在位图中设置已分配的标志 */
	__set_bit(asid, asid_map);
	/*
	 * 使用静态变量 cur_idx 记录刚分配的 ASID ，下次分配 ASID 时从这次分配的
	 * ASID 开始查找
	 */
	cur_idx = asid;
	return idx2asid(asid) | generation;
}

/*
 * 完成与架构相关的硬件设置，如是否需要给进程重新分配 ASID 、刷新 TLB 和设置硬件
 * 页表等
 */
void check_and_switch_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long flags;
	u64 asid, old_active_asid;

	if (system_supports_cnp())
		cpu_set_reserved_ttbr0();

    /*
     * 什么是 ASID ? 有什么用 ？
     * 为了支持进程独有类型的 TLB ，ARM 架构出现了一种硬件解决方案，叫作进程
     * 地址空间 ID(ASID)， TLB 可以识别哪些 TLB 项是属于某个进程的。 ASID 方
     * 案让每一个 TLB 表项包含一个 ASID ， ASID 用于标识每个进程的地址空间，
     * TLB 命中查询的标准在原来的虚拟地址判断之上，再加上 ASID 条件。因此有
     * 了 ASID 硬件机制的支持，进程切换不需要刷新整个 TLB ，即使 next 进程访
     * 问了相同的虚拟地址， prev 进程缓存的 TLB 项也不会影响到 next 进程，因
     * 为 ASID 机制从硬件上保证了 prev 进程的 next 进程的 TLB 不会产生冲突。
     */

	asid = atomic64_read(&mm->context.id);

	/*
	 * The memory ordering here is subtle.
	 * If our active_asids is non-zero and the ASID matches the current
	 * generation, then we update the active_asids entry with a relaxed
	 * cmpxchg. Racing with a concurrent rollover means that either:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on the
	 *   lock. Taking the lock synchronises with the rollover and so
	 *   we are forced to see the updated generation.
	 *
	 * - We get a valid ASID back from the cmpxchg, which means the
	 *   relaxed xchg in flush_context will treat us as reserved
	 *   because atomic RmWs are totally ordered for a given location.
	 */
	old_active_asid = atomic64_read(&per_cpu(active_asids, cpu));
	if (old_active_asid &&
        /* 异或来判断进程的 genaration 和全局的是否相等。相等为 0 ，不相等为 1 */
	    !((asid ^ atomic64_read(&asid_generation)) >> asid_bits) &&
	    /*
	     * 如果 active_asids 的旧值不是 0 ，那么说明前后两条语句之间，其他处理器
	     * 没有更新全局 asid generation ，可以执行快速路径(跳转到标号
	     * switch_mm_fastpath 去设置寄存器 TTBR0_EL1)。如果 active_asids 的旧值
	     * 是 0 ，说明说明前后两条语句之间其他处理器在分配 ASID 时把全局 asid
	     * generation 加 1 了，那么执行慢速路径。
	     */
	    atomic64_cmpxchg_relaxed(&per_cpu(active_asids, cpu),
				     old_active_asid, asid))
        /*
         * 全局原子变量 asid_generation 存储的软件 generation 计数和进程内
         * 存描述符存储的软件 generation 计数相同，说明换入进程的 ASID 还
         * 依然属于同一个批次，也就是说，还没有发生 ASID 硬件溢出，因此切
         * 换进程不需要任何的 TLB 刷新操作
         */
		goto switch_mm_fastpath;

	raw_spin_lock_irqsave(&cpu_asid_lock, flags);
	/* Check that our ASID belongs to the current generation. */
	asid = atomic64_read(&mm->context.id);
    /*
     * 在申请自旋锁 cpu_asid_lock 之后重新做一次进程和全局软件 generation 计数的
     * 比较，如果还是不相同，说明至少发生了一次 ASID 硬件溢出，需要分配一个新的
     * 软件 ASID 计数
     */
	if ((asid ^ atomic64_read(&asid_generation)) >> asid_bits) {
		asid = new_context(mm);
		atomic64_set(&mm->context.id, asid);
	}

    /*
     * ASID 硬件溢出，需要刷新本地的 TLB 。如果位图 tlb_flush_pending 中当前处理
     * 器对应的位被设置，那么把当前处理器的页表缓存清空。当全局 ASID 版本号加 1
     * 时，需要把所有处理器的页表缓存清空，在位图 tlb_flush_pending 中把所有处理
     * 器对应的位设置
     */
	if (cpumask_test_and_clear_cpu(cpu, &tlb_flush_pending))
		local_flush_tlb_all();

	/* 把当前处理器的 active_asids 设置为进程的 ASID */
	atomic64_set(&per_cpu(active_asids, cpu), asid);
	raw_spin_unlock_irqrestore(&cpu_asid_lock, flags);

switch_mm_fastpath:

	arm64_apply_bp_hardening();

	/*
	 * Defer TTBR0_EL1 setting for user threads to uaccess_enable() when
	 * emulating PAN.
	 */
	/*
	 * 如果不需要通过切换寄存器 TTBR0_EL1 仿真 PAN 特性，那么调用函数
	 * cpu_switch_mm 设置寄存器 TTBR0_EL1 ，否则延迟到进程从内核模式返回用户模式
	 * 时设置寄存器 TTBR0_EL1 。
	 */
	if (!system_uses_ttbr0_pan())
        /* 进行页表的切换 */
		cpu_switch_mm(mm->pgd, mm);
}

/* Errata workaround post TTBRx_EL1 update. */
asmlinkage void post_ttbr_update_workaround(void)
{
	asm(ALTERNATIVE("nop; nop; nop",
			"ic iallu; dsb nsh; isb",
			ARM64_WORKAROUND_CAVIUM_27456,
			CONFIG_CAVIUM_ERRATUM_27456));
}

static int asids_init(void)
{
	asid_bits = get_cpu_asid_bits();
	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more ASID than CPUs. ASID #0 is reserved for init_mm.
	 */
	WARN_ON(NUM_USER_ASIDS - 1 <= num_possible_cpus());
	atomic64_set(&asid_generation, ASID_FIRST_VERSION);
	asid_map = kcalloc(BITS_TO_LONGS(NUM_USER_ASIDS), sizeof(*asid_map),
			   GFP_KERNEL);
	if (!asid_map)
		panic("Failed to allocate bitmap for %lu ASIDs\n",
		      NUM_USER_ASIDS);

	pr_info("ASID allocator initialised with %lu entries\n", NUM_USER_ASIDS);
	return 0;
}
early_initcall(asids_init);
