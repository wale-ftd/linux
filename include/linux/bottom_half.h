/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BH_H
#define _LINUX_BH_H

#include <linux/preempt.h>

#ifdef CONFIG_TRACE_IRQFLAGS
extern void __local_bh_disable_ip(unsigned long ip, unsigned int cnt);
#else
static __always_inline void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
    /* 进入软中断上下文 */
	preempt_count_add(cnt);
    /*
     * 防止编译器做优化。 thread_info->preempt_count 相当于 Per-CPU 变量，因此
     * 不需要使用内存屏障指令。
     */
	barrier();
}
#endif

/*
 * 关闭软中断。和 local_bh_enable 组成的临界区禁止本地 CPU 在中断返回前
 * (irq_exit())执行软中断
 */
static inline void local_bh_disable(void)
{
	__local_bh_disable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
}

extern void _local_bh_enable(void);
extern void __local_bh_enable_ip(unsigned long ip, unsigned int cnt);

static inline void local_bh_enable_ip(unsigned long ip)
{
	__local_bh_enable_ip(ip, SOFTIRQ_DISABLE_OFFSET);
}

/* 打开软中断 */
static inline void local_bh_enable(void)
{
	__local_bh_enable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
}

#endif /* _LINUX_BH_H */
