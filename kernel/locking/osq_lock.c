// SPDX-License-Identifier: GPL-2.0
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/osq_lock.h>

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Using a single mcs node per CPU is safe because sleeping locks should not be
 * called from interrupt context and we have preemption disabled while
 * spinning.
 */
static DEFINE_PER_CPU_SHARED_ALIGNED(struct optimistic_spin_node, osq_node);

/*
 * We use the value 0 to represent "no CPU", thus the encoded value
 * will be the CPU number incremented by 1.
 */
static inline int encode_cpu(int cpu_nr)
{
	return cpu_nr + 1;
}

static inline int node_cpu(struct optimistic_spin_node *node)
{
	return node->cpu - 1;
}

static inline struct optimistic_spin_node *decode_cpu(int encoded_cpu_val)
{
	int cpu_nr = encoded_cpu_val - 1;

	return per_cpu_ptr(&osq_node, cpu_nr);
}

/*
 * Get a stable @node->next pointer, either for unlock() or unqueue() purposes.
 * Can return NULL in case we were the last queued and we updated @lock instead.
 */
static inline struct optimistic_spin_node *
osq_wait_next(struct optimistic_spin_queue *lock,
	      struct optimistic_spin_node *node,
	      struct optimistic_spin_node *prev)
{
	struct optimistic_spin_node *next = NULL;
	int curr = encode_cpu(smp_processor_id());
	int old;

	/*
	 * If there is a prev node in queue, then the 'old' value will be
	 * the prev node's CPU #, else it's set to OSQ_UNLOCKED_VAL since if
	 * we're currently last in queue, then the queue will then become empty.
	 */
	old = prev ? prev->cpu : OSQ_UNLOCKED_VAL;

	for (;;) {
		if (atomic_read(&lock->tail) == curr &&
		    atomic_cmpxchg_acquire(&lock->tail, curr, old) == curr) {
		/* 当前节点是 MCS 链表中的最后一个节点 */
			/*
			 * We were the last queued, we moved @lock back. @prev
			 * will now observe @lock and will complete its
			 * unlock()/unqueue().
			 */
			break;
		}

		/*
		 * We must xchg() the @node->next value, because if we were to
		 * leave it in, a concurrent unlock()/unqueue() from
		 * @node->next might complete Step-A and think its @prev is
		 * still valid.
		 *
		 * If the concurrent unlock()/unqueue() wins the race, we'll
		 * wait for either @lock to point to us, through its Step-B, or
		 * wait for a new @node->next from its Step-C.
		 */
		if (node->next) {
			next = xchg(&node->next, NULL);
			if (next)
				break;
		}

		cpu_relax();
	}

	return next;
}

bool osq_lock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_node *node = this_cpu_ptr(&osq_node);
	struct optimistic_spin_node *prev, *next;
	int curr = encode_cpu(smp_processor_id());
	int old;

	node->locked = 0;
	node->next = NULL;
	node->cpu = curr;

	/*
	 * We need both ACQUIRE (pairs with corresponding RELEASE in
	 * unlock() uncontended, or fastpath) and RELEASE (to publish
	 * the node fields we just initialised) semantics when updating
	 * the lock tail.
	 */
	old = atomic_xchg(&lock->tail, curr);
	if (old == OSQ_UNLOCKED_VAL)
		return true;

	prev = decode_cpu(old);
	node->prev = prev;

	/*
	 * osq_lock()			unqueue
	 *
	 * node->prev = prev		osq_wait_next()
	 * WMB				MB
	 * prev->next = node		next->prev = prev // unqueue-C
	 *
	 * Here 'node->prev' and 'next->prev' are the same variable and we need
	 * to ensure these stores happen in-order to avoid corrupting the list.
	 */
	smp_wmb();

	WRITE_ONCE(prev->next, node);

	/*
	 * Normally @prev is untouchable after the above store; because at that
	 * moment unlock can proceed and wipe the node element from stack.
	 *
	 * However, since our nodes are static per-cpu storage, we're
	 * guaranteed their existence -- this allows us to apply
	 * cmpxchg in an attempt to undo our queueing.
	 */

	/*
	 * 查询当前 cpu 的 node->locked 是否变成了 1 ，因为前一个 node 释放锁时会
	 * 把它的下一个 node->locked 成员设置为 1
	 */
	while (!READ_ONCE(node->locked)) {
		/*
		 * If we need to reschedule bail... so we can block.
		 * Use vcpu_is_preempted() to avoid waiting for a preempted
		 * lock holder:
		 */
		if (need_resched() || vcpu_is_preempted(node_cpu(node->prev)))
		/*
		 * 自旋等待过程中，有更高优先级的进程抢占或者被调度出去，应该放弃自旋
		 * 等待，退出 MCS 链表
		 */
			goto unqueue;

		cpu_relax();
	}
	return true;

/*
 * 实现删除链表等操作，这里仅使用了原子比较并交换指令，并没有使用其他的锁，体
 * 现了无锁并发编程的精髓
 */
unqueue:
	/*
	 * Step - A  -- stabilize @prev
	 *
	 * Undo our @prev->next assignment; this will make @prev's
	 * unlock()/unqueue() wait for a next pointer since @lock points to us
	 * (or later).
	 */

	/* 处理前继节点的 next 指针指向问题 */
	for (;;) {
		if (prev->next == node &&
		    cmpxchg(&prev->next, node, NULL) == node)
		/*
		 * prev->next == node ，说明上面 WRITE_ONCE(prev->next, node) 到此，
		 * 这段时间 MCS 链表没有被修改。接着用 cmpxchg()，如果交换前
		 * prev->next 的值为 node ，则当前节点成功获取锁
		 */
			break;

		/*
		 * We can only fail the cmpxchg() racing against an unlock(),
		 * in which case we should observe @node->locked becomming
		 * true.
		 */
		/*
		 * MCS 链表被修改了。再次判断当前节点是否持有了锁。 smp_load_acquire()
		 * 会强制重新从内存加载 node->locked 的值
		 */
		if (smp_load_acquire(&node->locked))
		/*
		 * 为什么当前节点莫名其妙地持有了锁呢？因为前继节点释放了锁并且把锁传
		 * 递给了当前节点
		 */
			return true;

		cpu_relax();

		/*
		 * Or we race against a concurrent unqueue()'s step-B, in which
		 * case its step-C will write us a new @node->prev pointer.
		 */
		prev = READ_ONCE(node->prev);
	}

	/*
	 * Step - B -- stabilize @next
	 *
	 * Similar to unlock(), wait for @node->next or move @lock from @node
	 * back to @prev.
	 */

	/* 处理当前节点的 next 指针指向问题 */
	next = osq_wait_next(lock, node, prev);
	if (!next)
		return false;

	/*
	 * Step - C -- unlink
	 *
	 * @prev is stable because its still waiting for a new @prev->next
	 * pointer, @next is stable because our @node->next pointer is NULL and
	 * it will wait in Step-A.
	 */

	WRITE_ONCE(next->prev, prev);
	WRITE_ONCE(prev->next, next);

	return false;
}

void osq_unlock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_node *node, *next;
	int curr = encode_cpu(smp_processor_id());

	/*
	 * Fast path for the uncontended case.
	 */
	if (likely(atomic_cmpxchg_release(&lock->tail, curr,
					  OSQ_UNLOCKED_VAL) == curr))
		return;

	/*
	 * Second most likely case.
	 */
	node = this_cpu_ptr(&osq_node);
	next = xchg(&node->next, NULL);
	if (next) {
		/* 把锁传递给后继节点 */
		WRITE_ONCE(next->locked, 1);
		return;
	}

	/*
	 * 如果后继节点为空，说明在执行 osq_unlock()期间有成员擅自离队，那么只能
	 * 调用 osq_wait_next()来确定或者等待确定的后继节点，也许当前节点就在链表
	 * 末尾
	 */
	next = osq_wait_next(lock, node, NULL);
	if (next)
		/* 把锁传递给后继节点 */
		WRITE_ONCE(next->locked, 1);
}
