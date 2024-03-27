/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_TOPOLOGY_H
#define _LINUX_SCHED_TOPOLOGY_H

#include <linux/topology.h>

#include <linux/sched/idle.h>

/*
 * Increase resolution of cpu_capacity calculations
 */
#define SCHED_CAPACITY_SHIFT	SCHED_FIXEDPOINT_SHIFT
/* 系统中最强的 CPU 的量化计算能力 */
#define SCHED_CAPACITY_SCALE	(1L << SCHED_CAPACITY_SHIFT)

/*
 * sched-domains (multiprocessor balancing) declarations:
 */
#ifdef CONFIG_SMP

/* 表示该调度域运行后做负载均衡调度 */
#define SD_LOAD_BALANCE		0x0001	/* Do load balancing on this domain. */
/* 表示当 CPU 变成空闲后做负载均衡调度 */
#define SD_BALANCE_NEWIDLE	0x0002	/* Balance when about to become idle */
/* 表示一个进程调用 exec 时会重新选择一个最优的 CPU 来运行，详见 sched_exec() */
#define SD_BALANCE_EXEC		0x0004	/* Balance on exec */
/* 表示派生一个新进程后会选择一个最优的 CPU 来运行新进程，详见 wake_up_new_task() */
#define SD_BALANCE_FORK		0x0008	/* Balance on fork, clone */
/* 表示唤醒一个进程时会选择一个最优的 CPU 来唤醒该进程，详见 wake_up_process() */
#define SD_BALANCE_WAKE		0x0010  /* Balance on wakeup */
/* 支持 wake affine 特性 */
#define SD_WAKE_AFFINE		0x0020	/* Wake task to waking CPU */
/* 表示该调度域有不同架构的 CPU ，如大/小核 */
#define SD_ASYM_CPUCAPACITY	0x0040  /* Domain members have different CPU capacities */
/* 表示该调度域中的 CPU 都是可以共享 CPU 资源的，主要用于描述 SMT 调度层级 */
#define SD_SHARE_CPUCAPACITY	0x0080	/* Domain members share CPU capacity */
/* 表示该调度域的 CPU 可以共享电源域 */
#define SD_SHARE_POWERDOMAIN	0x0100	/* Domain members share power domain */
/* 表示该调度域的 CPU 可以共享高速缓存 */
#define SD_SHARE_PKG_RESOURCES	0x0200	/* Domain members share CPU pkg resources */
#define SD_SERIALIZE		0x0400	/* Only a single load balancing instance */
/* 用于描述与 SMT 调度层级相关的一些例外 */
#define SD_ASYM_PACKING		0x0800  /* Place busy groups earlier in the domain */
/* 表示可以在兄弟调度域中迁移进程 */
#define SD_PREFER_SIBLING	0x1000	/* Prefer to place tasks in a sibling domain */
#define SD_OVERLAP		0x2000	/* sched_domains of this level overlap */
/* 用于描述 NUMA 调度层级 */
#define SD_NUMA			0x4000	/* cross-node balancing */

#ifdef CONFIG_SCHED_SMT
static inline int cpu_smt_flags(void)
{
	return SD_SHARE_CPUCAPACITY | SD_SHARE_PKG_RESOURCES;
}
#endif

#ifdef CONFIG_SCHED_MC
static inline int cpu_core_flags(void)
{
	return SD_SHARE_PKG_RESOURCES;
}
#endif

#ifdef CONFIG_NUMA
static inline int cpu_numa_flags(void)
{
	return SD_NUMA;
}
#endif

extern int arch_asym_cpu_priority(int cpu);

struct sched_domain_attr {
	int relax_domain_level;
};

#define SD_ATTR_INIT	(struct sched_domain_attr) {	\
	.relax_domain_level = -1,			\
}

extern int sched_domain_level_max;

struct sched_group;

struct sched_domain_shared {
	atomic_t	ref;
	atomic_t	nr_busy_cpus;
	int		has_idle_cores;
};

/* 描述调度层级 */
struct sched_domain {
	/* These fields must be setup */
	struct sched_domain *parent;	/* top domain must be null terminated */
	struct sched_domain *child;	/* bottom domain must be null terminated */
	struct sched_group *groups;	/* the balancing groups of the domain */
	unsigned long min_interval;	/* Minimum balance interval ms */
	unsigned long max_interval;	/* Maximum balance interval ms */
	unsigned int busy_factor;	/* less balancing by factor if busy */
	unsigned int imbalance_pct;	/* No balance until over watermark */
	unsigned int cache_nice_tries;	/* Leave cache hot tasks for # tries */
	unsigned int busy_idx;
	unsigned int idle_idx;
	unsigned int newidle_idx;
	unsigned int wake_idx;
	unsigned int forkexec_idx;

	int nohz_idle;			/* NOHZ IDLE status */
	int flags;			/* See SD_* */
	int level;

	/* Runtime fields. */
	unsigned long last_balance;	/* init to jiffies. units in jiffies */
	unsigned int balance_interval;	/* initialise to 1. units in ms. */
	unsigned int nr_balance_failed; /* initialise to 0 */

	/* idle_balance() stats */
	u64 max_newidle_lb_cost;
	unsigned long next_decay_max_lb_cost;

	u64 avg_scan_cost;		/* select_idle_sibling */

#ifdef CONFIG_SCHEDSTATS
	/* load_balance() stats */
	unsigned int lb_count[CPU_MAX_IDLE_TYPES];
	unsigned int lb_failed[CPU_MAX_IDLE_TYPES];
	unsigned int lb_balanced[CPU_MAX_IDLE_TYPES];
	unsigned int lb_imbalance[CPU_MAX_IDLE_TYPES];
	unsigned int lb_gained[CPU_MAX_IDLE_TYPES];
	unsigned int lb_hot_gained[CPU_MAX_IDLE_TYPES];
	unsigned int lb_nobusyg[CPU_MAX_IDLE_TYPES];
	unsigned int lb_nobusyq[CPU_MAX_IDLE_TYPES];

	/* Active load balancing */
	unsigned int alb_count;
	unsigned int alb_failed;
	unsigned int alb_pushed;

	/* SD_BALANCE_EXEC stats */
	unsigned int sbe_count;
	unsigned int sbe_balanced;
	unsigned int sbe_pushed;

	/* SD_BALANCE_FORK stats */
	unsigned int sbf_count;
	unsigned int sbf_balanced;
	unsigned int sbf_pushed;

	/* try_to_wake_up() stats */
	unsigned int ttwu_wake_remote;
	unsigned int ttwu_move_affine;
	unsigned int ttwu_move_balance;
#endif
#ifdef CONFIG_SCHED_DEBUG
	char *name;
#endif
	union {
		void *private;		/* used during construction */
		struct rcu_head rcu;	/* used during destruction */
	};
	struct sched_domain_shared *shared;

	unsigned int span_weight;
	/*
	 * Span of all CPUs in this domain.
	 *
	 * NOTE: this field is variable length. (Allocated dynamically
	 * by attaching extra space to the end of the structure,
	 * depending on how many CPUs the kernel has booted up with)
	 */
	unsigned long span[0];
};

static inline struct cpumask *sched_domain_span(struct sched_domain *sd)
{
	return to_cpumask(sd->span);
}

extern void partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
				    struct sched_domain_attr *dattr_new);

/* Allocate an array of sched domains, for partition_sched_domains(). */
cpumask_var_t *alloc_sched_domains(unsigned int ndoms);
void free_sched_domains(cpumask_var_t doms[], unsigned int ndoms);

bool cpus_share_cache(int this_cpu, int that_cpu);

typedef const struct cpumask *(*sched_domain_mask_f)(int cpu);
typedef int (*sched_domain_flags_f)(void);

#define SDTL_OVERLAP	0x01

struct sd_data {
	struct sched_domain **__percpu sd;
	struct sched_domain_shared **__percpu sds;
	struct sched_group **__percpu sg;
	struct sched_group_capacity **__percpu sgc;
};

/* 描述 CPU 的层次关系 */
struct sched_domain_topology_level {
	sched_domain_mask_f mask;
	sched_domain_flags_f sd_flags;
	int		    flags;
	int		    numa_level;
	struct sd_data      data;
#ifdef CONFIG_SCHED_DEBUG
	char                *name;
#endif
};

extern void set_sched_topology(struct sched_domain_topology_level *tl);

#ifdef CONFIG_SCHED_DEBUG
# define SD_INIT_NAME(type)		.name = #type
#else
# define SD_INIT_NAME(type)
#endif

#ifndef arch_scale_cpu_capacity
static __always_inline
unsigned long arch_scale_cpu_capacity(struct sched_domain *sd, int cpu)
{
	return SCHED_CAPACITY_SCALE;
}
#endif

#else /* CONFIG_SMP */

struct sched_domain_attr;

static inline void
partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
			struct sched_domain_attr *dattr_new)
{
}

static inline bool cpus_share_cache(int this_cpu, int that_cpu)
{
	return true;
}

#ifndef arch_scale_cpu_capacity
static __always_inline
unsigned long arch_scale_cpu_capacity(void __always_unused *sd, int cpu)
{
	return SCHED_CAPACITY_SCALE;
}
#endif

#endif	/* !CONFIG_SMP */

static inline int task_node(const struct task_struct *p)
{
	return cpu_to_node(task_cpu(p));
}

#endif /* _LINUX_SCHED_TOPOLOGY_H */
