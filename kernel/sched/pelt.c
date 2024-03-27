// SPDX-License-Identifier: GPL-2.0
/*
 * Per Entity Load Tracking
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
 *
 *  Move PELT related code from fair.c into this pelt.c file
 *  Author: Vincent Guittot <vincent.guittot@linaro.org>
 */

#include <linux/sched.h>
#include "sched.h"
#include "sched-pelt.h"
#include "pelt.h"

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
/*
 * 计算第 n 个周期的衰减值 val * y^n 。其中一个周期为 1024us ~= 1ms
 * @val: n 个周期前的负载值
 * @n: 第 n 个周期
 */
static u64 decay_load(u64 val, u64 n)
{
	unsigned int local_n;

	if (unlikely(n > LOAD_AVG_PERIOD * 63))
    /* 周期大于 32*63 = 2016 ms */
		return 0;

	/* after bounds checking we can collapse to 32-bit */
	local_n = n;

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
	 * With a look-up table which covers y^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 */
	/* 处理周期大于等于 32ms 部分 */
	if (unlikely(local_n >= LOAD_AVG_PERIOD)) {
    /* 如果周期处于 32 ~ 2016 ms ，第增加 32 ms 就要衰减一半，相当于右移一位 */
		val >>= local_n / LOAD_AVG_PERIOD;
		local_n %= LOAD_AVG_PERIOD;
	}

	/* 处理周期小于 32ms 部分 */
	val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);
	return val;
}

static u32 __accumulate_pelt_segments(u64 periods, u32 d1, u32 d3)
{
	u32 c1, c2, c3 = d3; /* y^0 == 1 */

	/*
	 * c1 = d1 y^p
	 */
	c1 = decay_load((u64)d1, periods);

	/*
	 *            p-1
	 * c2 = 1024 \Sum y^n
	 *            n=1
	 *
	 *              inf        inf
	 *    = 1024 ( \Sum y^n - \Sum y^n - y^0 )
	 *              n=0        n=p
	 */
	/*
	 *
	 *         inf                 inf                     inf
	 * 1024 ( \Sum y^n ) = 1024 ( \Sum y^(n+p) ) = 1024 ( \Sum y^n ) y^p
	 *         n=p                 n=0                     n=0
	 *
	 *                   = LOAD_AVG_MAX * y^p
	 * 详见提交: 05296e7535d67ba4926b543a09cf5d430a815cb6
	 */
	c2 = LOAD_AVG_MAX - decay_load(LOAD_AVG_MAX, periods) - 1024;

	return c1 + c2 + c3;
}

#define cap_scale(v, s) ((v)*(s) >> SCHED_CAPACITY_SHIFT)

/*
 * Accumulate the three separate parts of the sum; d1 the remainder
 * of the last (incomplete) period, d2 the span of full periods and d3
 * the remainder of the (incomplete) current period.
 *
 *           d1          d2           d3
 *           ^           ^            ^
 *           |           |            |
 *         |<->|<----------------->|<--->|
 * ... |---x---|------| ... |------|-----x (now)
 *
 *                           p-1
 * u' = (u + d1) y^p + 1024 \Sum y^n + d3 y^0
 *                           n=1
 *
 *    = u y^p +					(Step 1)
 *
 *                     p-1
 *      d1 y^p + 1024 \Sum y^n + d3 y^0		(Step 2)
 *                     n=1
 */
static __always_inline u32
accumulate_sum(u64 delta, int cpu, struct sched_avg *sa,
	       unsigned long load, unsigned long runnable, int running)
{
	unsigned long scale_freq, scale_cpu;
	u32 contrib = (u32)delta; /* p == 0 -> delta < 1024 */
	u64 periods;

    /* EAS 引入的。用于计算 CPU 在不同频率下的计算能力 */
	scale_freq = arch_scale_freq_capacity(cpu);
    /* 用于计算不同的 CPU 的额定计算能力，因为大/小核的 CPU 是不一样的 */
	scale_cpu = arch_scale_cpu_capacity(NULL, cpu);

    /* 加上上一次更新时不能凑成一个周期(1024us)的多余时间 */
	delta += sa->period_contrib;
    /* 计算完整的周期数 */
	periods = delta / 1024; /* A period is 1024us (~1ms) */

	/*
	 * Step 1: decay old *_sum if we crossed period boundaries.
	 */
	if (periods) {
		sa->load_sum = decay_load(sa->load_sum, periods);
		sa->runnable_load_sum =
			decay_load(sa->runnable_load_sum, periods);
		sa->util_sum = decay_load((u64)(sa->util_sum), periods);

		/*
		 * Step 2
		 */
		/* 计算这次更新时不能凑成一个周期(1024us)的多余时间 */
		delta %= 1024;
		/* 计算自从上次计算负载总和到现在这段时间的负载增量 */
		contrib = __accumulate_pelt_segments(periods,
				1024 - sa->period_contrib, delta);
	}
	sa->period_contrib = delta;

	contrib = cap_scale(contrib, scale_freq);
	if (load)
		sa->load_sum += load * contrib;
	if (runnable)
		sa->runnable_load_sum += runnable * contrib;
	if (running)
        /* 为了计算实际算力，还需要乘以 scale_cpu */
		sa->util_sum += contrib * scale_cpu;

	return periods;
}

/*
 * We can represent the historical contribution to runnable average as the
 * coefficients of a geometric series.  To do this we sub-divide our runnable
 * history into segments of approximately 1ms (1024us); label the segment that
 * occurred N-ms ago p_N, with p_0 corresponding to the current period, e.g.
 *
 * [<- 1024us ->|<- 1024us ->|<- 1024us ->| ...
 *      p0            p1           p2
 *     (now)       (~1ms ago)  (~2ms ago)
 *
 * Let u_i denote the fraction of p_i that the entity was runnable.
 *
 * We then designate the fractions u_i as our co-efficients, yielding the
 * following representation of historical load:
 *   u_0 + u_1*y + u_2*y^2 + u_3*y^3 + ...
 *
 * We choose y based on the with of a reasonably scheduling period, fixing:
 *   y^32 = 0.5
 *
 * This means that the contribution to load ~32ms ago (u_32) will be weighted
 * approximately half as much as the contribution to load within the last ms
 * (u_0).
 *
 * When a period "rolls over" and we have new u_0`, multiplying the previous
 * sum again by y is sufficient to update:
 *   load_avg = u_0` + y*(u_0 + u_1*y + u_2*y^2 + ... )
 *            = u_0 + u_1*y + u_2*y^2 + ... [re-labeling u_i --> u_{i+1}]
 */
static __always_inline int
___update_load_sum(u64 now, int cpu, struct sched_avg *sa,
		  unsigned long load, unsigned long runnable, int running)
{
	u64 delta;

	delta = now - sa->last_update_time;
	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		sa->last_update_time = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 */
	delta >>= 10;
	if (!delta)
		return 0;

	sa->last_update_time += delta << 10;

	/*
	 * running is a subset of runnable (weight) so running can't be set if
	 * runnable is clear. But there are some corner cases where the current
	 * se has been already dequeued but cfs_rq->curr still points to it.
	 * This means that weight will be 0 but not running for a sched_entity
	 * but also for a cfs_rq if the latter becomes idle. As an example,
	 * this happens during idle_balance() which calls
	 * update_blocked_averages()
	 */
	if (!load)
		runnable = running = 0;

	/*
	 * Now we know we crossed measurement unit boundaries. The *_avg
	 * accrues by two steps:
	 *
	 * Step 1: accumulate *_sum since last_update_time. If we haven't
	 * crossed period boundaries, finish.
	 */
	/*
	 * 计算负载总和。如果时间间隔 delta 至少包含一个完整周期 1024us ，那么
	 * 函数 accumulate_sum 返回 true ，否则返回 false 。
	 * Step 2 见 ___update_load_avg()
	 */
	if (!accumulate_sum(delta, cpu, sa, load, runnable, running))
		return 0;

	return 1;
}

static __always_inline void
___update_load_avg(struct sched_avg *sa, unsigned long load, unsigned long runnable)
{
	/*
	 * 为什么要减去 (1024 - sa->period_contrib) 呢？因为它是上一次采样的时候
	 * 不满一个周期残留下来的，这段时间不能不考虑，而 LOAD_AVG_MAX 是从一个
	 * 完整的周期开始计算的，所以要把这部分残余减去。
	 */
	u32 divider = LOAD_AVG_MAX - 1024 + sa->period_contrib;

	/*
	 * Step 2: update *_avg.
	 */
	sa->load_avg = div_u64(load * sa->load_sum, divider);
	sa->runnable_load_avg =	div_u64(runnable * sa->runnable_load_sum, divider);
	WRITE_ONCE(sa->util_avg, sa->util_sum / divider);
}

/*
 * sched_entity:
 *
 *   task:
 *     se_runnable() == se_weight()
 *
 *   group: [ see update_cfs_group() ]
 *     se_weight()   = tg->weight * grq->load_avg / tg->load_avg
 *     se_runnable() = se_weight(se) * grq->runnable_load_avg / grq->load_avg
 *
 *   load_sum := runnable_sum
 *   load_avg = se_weight(se) * runnable_avg
 *
 *   runnable_load_sum := runnable_sum
 *   runnable_load_avg = se_runnable(se) * runnable_avg
 *
 * XXX collapse load_sum and runnable_load_sum
 *
 * cfq_rq:
 *
 *   load_sum = \Sum se_weight(se) * se->avg.load_sum
 *   load_avg = \Sum se->avg.load_avg
 *
 *   runnable_load_sum = \Sum se_runnable(se) * se->avg.runnable_load_sum
 *   runnable_load_avg = \Sum se->avg.runable_load_avg
 */

int __update_load_avg_blocked_se(u64 now, int cpu, struct sched_entity *se)
{
	if (___update_load_sum(now, cpu, &se->avg, 0, 0, 0)) {
		___update_load_avg(&se->avg, se_weight(se), se_runnable(se));
		return 1;
	}

	return 0;
}

int __update_load_avg_se(u64 now, int cpu, struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (___update_load_sum(now, cpu, &se->avg, !!se->on_rq, !!se->on_rq,
				cfs_rq->curr == se)) {

		___update_load_avg(&se->avg, se_weight(se), se_runnable(se));
		cfs_se_util_change(&se->avg);
		return 1;
	}

	return 0;
}

int __update_load_avg_cfs_rq(u64 now, int cpu, struct cfs_rq *cfs_rq)
{
	if (___update_load_sum(now, cpu, &cfs_rq->avg,
				scale_load_down(cfs_rq->load.weight),
				scale_load_down(cfs_rq->runnable_weight),
				cfs_rq->curr != NULL)) {

		___update_load_avg(&cfs_rq->avg, 1, 1);
		return 1;
	}

	return 0;
}

/*
 * rt_rq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_load_sum = load_sum
 *
 *   load_avg and runnable_load_avg are not supported and meaningless.
 *
 */

int update_rt_rq_load_avg(u64 now, struct rq *rq, int running)
{
	if (___update_load_sum(now, rq->cpu, &rq->avg_rt,
				running,
				running,
				running)) {

		___update_load_avg(&rq->avg_rt, 1, 1);
		return 1;
	}

	return 0;
}

/*
 * dl_rq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_load_sum = load_sum
 *
 */

int update_dl_rq_load_avg(u64 now, struct rq *rq, int running)
{
	if (___update_load_sum(now, rq->cpu, &rq->avg_dl,
				running,
				running,
				running)) {

		___update_load_avg(&rq->avg_dl, 1, 1);
		return 1;
	}

	return 0;
}

#ifdef CONFIG_HAVE_SCHED_AVG_IRQ
/*
 * irq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_load_sum = load_sum
 *
 */

int update_irq_load_avg(struct rq *rq, u64 running)
{
	int ret = 0;
	/*
	 * We know the time that has been used by interrupt since last update
	 * but we don't when. Let be pessimistic and assume that interrupt has
	 * happened just before the update. This is not so far from reality
	 * because interrupt will most probably wake up task and trig an update
	 * of rq clock during which the metric si updated.
	 * We start to decay with normal context time and then we add the
	 * interrupt context time.
	 * We can safely remove running from rq->clock because
	 * rq->clock += delta with delta >= running
	 */
	ret = ___update_load_sum(rq->clock - running, rq->cpu, &rq->avg_irq,
				0,
				0,
				0);
	ret += ___update_load_sum(rq->clock, rq->cpu, &rq->avg_irq,
				1,
				1,
				1);

	if (ret)
		___update_load_avg(&rq->avg_irq, 1, 1);

	return ret;
}
#endif
