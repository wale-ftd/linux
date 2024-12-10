/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
   md.h : kernel internal structure of the Linux MD driver
          Copyright (C) 1996-98 Ingo Molnar, Gadi Oxman

*/

#ifndef _MD_MD_H
#define _MD_MD_H

#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/badblocks.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "md-cluster.h"

#define MaxSector (~(sector_t)0)

/*
 * These flags should really be called "NO_RETRY" rather than
 * "FAILFAST" because they don't make any promise about time lapse,
 * only about the number of retries, which will be zero.
 * REQ_FAILFAST_DRIVER is not included because
 * Commit: 4a27446f3e39 ("[SCSI] modify scsi to handle new fail fast flags.")
 * seems to suggest that the errors it avoids retrying should usually
 * be retried.
 */
#define	MD_FAILFAST	(REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT)

/*
 * The struct embedded in rdev is used to serialize IO.
 */
struct serial_in_rdev {
	struct rb_root_cached serial_rb;
	spinlock_t serial_lock;
	wait_queue_head_t serial_io_wait;
};

/*
 * MD's 'extended' device
 */
/* md 设备成员磁盘的抽象 */
struct md_rdev {
	struct list_head same_set;	/* RAID devices within the same set */

	/*
	 * 成员磁盘的有效大小(MD 可用的大小)。
	 * 成员磁盘的实际大小 = 有效大小 + 超块大小 + 非 64K 对齐空间
	 */
	sector_t sectors;		/* Device size (in 512bytes sectors) */
	struct mddev *mddev;		/* RAID array if running */
	int last_events;		/* IO event timestamp */

	/*
	 * If meta_bdev is non-NULL, it means that a separate device is
	 * being used to store the metadata (superblock/bitmap) which
	 * would otherwise be contained on the same device as the data (bdev).
	 */
	struct block_device *meta_bdev;
	struct block_device *bdev;	/* block device handle */

	struct page	*sb_page, *bb_page;
	int		sb_loaded;
	__u64		sb_events;
	sector_t	data_offset;	/* start of data in array */
	sector_t	new_data_offset;/* only relevant while reshaping */
	/*
	 * +---------------------------------+----+-----+-----+
	 * |				     |	  |	|     |
	 * +---------------------------------+----+-----+-----+
	 * |		   s1		     | s2 |  s3 |  s4 |
	 * s1: 等于 sb_start ，也是磁盘有效长度
	 * s2+s3: md 保留空间，其中 s2 用于存放 raid 超级块，也为 sb_size
	 * s4: 磁盘大小 64K 对齐后的多余部分
	 * 磁盘大小是 s1+s2+s3+s4
	 */
	sector_t	sb_start;	/* offset of the super block (in 512byte sectors) */
	int		sb_size;	/* bytes in the superblock */
	int		preferred_minor;	/* autorun support */

	struct kobject	kobj;

	/* A device can be in one of three states based on two flags:
	 * Not working:   faulty==1 in_sync==0
	 * Fully working: faulty==0 in_sync==1
	 * Working, but not
	 * in sync with array
	 *                faulty==0 in_sync==0
	 *
	 * It can never have faulty==1, in_sync==1
	 * This reduces the burden of testing multiple flags in many cases
	 */

	unsigned long	flags;	/* bit set of 'enum flag_bits' bits. */
	wait_queue_head_t blocked_wait;

	/* /proc/mdstat 里可以查看 */
	int desc_nr;			/* descriptor index in the superblock */
	int raid_disk;			/* role of device in array */
	int new_raid_disk;		/* role that the device will have in
					 * the array after a level-change completes.
					 */
	int saved_raid_disk;		/* role that device used to have in the
					 * array and could again if we did a partial
					 * resync from the bitmap
					 */
	union {
		sector_t recovery_offset;/* If this device has been partially
					 * recovered, this is where we were
					 * up to.
					 */
		sector_t journal_tail;	/* If this device is a journal device,
					 * this is the journal tail (journal
					 * recovery start point)
					 */
	};

	atomic_t	nr_pending;	/* number of pending requests.
					 * only maintained for arrays that
					 * support hot removal
					 */
	atomic_t	read_errors;	/* number of consecutive read errors that
					 * we have tried to ignore.
					 */
	time64_t	last_read_error;	/* monotonic time since our
						 * last read error
						 */
	atomic_t	corrected_errors; /* number of corrected read errors,
					   * for reporting to userspace and storing
					   * in superblock.
					   */

	struct serial_in_rdev *serial;  /* used for raid1 io serialization */

	struct kernfs_node *sysfs_state; /* handle for 'state'
					   * sysfs entry */
	/* handle for 'unacknowledged_bad_blocks' sysfs dentry */
	struct kernfs_node *sysfs_unack_badblocks;
	/* handle for 'bad_blocks' sysfs dentry */
	struct kernfs_node *sysfs_badblocks;
	struct badblocks badblocks;

	struct {
		short offset;	/* Offset from superblock to start of PPL.
				 * Not used by external metadata. */
		unsigned int size;	/* Size in sectors of the PPL space */
		sector_t sector;	/* First sector of the PPL space */
	} ppl;
};
enum flag_bits {
	Faulty,			/* device is known to have a fault */
	In_sync,		/* device is in_sync with rest of array */
	Bitmap_sync,		/* ..actually, not quite In_sync.  Need a
				 * bitmap-based recovery to get fully in sync.
				 * The bit is only meaningful before device
				 * has been passed to pers->hot_add_disk.
				 */
	WriteMostly,		/* Avoid reading if at all possible */
	AutoDetected,		/* added by auto-detect */
	Blocked,		/* An error occurred but has not yet
				 * been acknowledged by the metadata
				 * handler, so don't allow writes
				 * until it is cleared */
	WriteErrorSeen,		/* A write error has been seen on this
				 * device
				 */
	FaultRecorded,		/* Intermediate state for clearing
				 * Blocked.  The Fault is/will-be
				 * recorded in the metadata, but that
				 * metadata hasn't been stored safely
				 * on disk yet.
				 */
	BlockedBadBlocks,	/* A writer is blocked because they
				 * found an unacknowledged bad-block.
				 * This can safely be cleared at any
				 * time, and the writer will re-check.
				 * It may be set at any time, and at
				 * worst the writer will timeout and
				 * re-check.  So setting it as
				 * accurately as possible is good, but
				 * not absolutely critical.
				 */
	WantReplacement,	/* This device is a candidate to be
				 * hot-replaced, either because it has
				 * reported some faults, or because
				 * of explicit request.
				 */
	Replacement,		/* This device is a replacement for
				 * a want_replacement device with same
				 * raid_disk number.
				 */
	Candidate,		/* For clustered environments only:
				 * This device is seen locally but not
				 * by the whole cluster
				 */
	Journal,		/* This device is used as journal for
				 * raid-5/6.
				 * Usually, this device should be faster
				 * than other devices in the array
				 */
	ClusterRemove,
	RemoveSynchronized,	/* synchronize_rcu() was called after
				 * this device was known to be faulty,
				 * so it is safe to remove without
				 * another synchronize_rcu() call.
				 */
	ExternalBbl,            /* External metadata provides bad
				 * block management for a disk
				 */
	FailFast,		/* Minimal retries should be attempted on
				 * this device, so use REQ_FAILFAST_DEV.
				 * Also don't try to repair failed reads.
				 * It is expects that no bad block log
				 * is present.
				 */
	LastDev,		/* Seems to be the last working dev as
				 * it didn't fail, so don't use FailFast
				 * any more for metadata
				 */
	CollisionCheck,		/*
				 * check if there is collision between raid1
				 * serial bios.
				 */
	Holder,			/* rdev is used as holder while opening
				 * underlying disk exclusively.
				 */
};

static inline int is_badblock(struct md_rdev *rdev, sector_t s, int sectors,
			      sector_t *first_bad, int *bad_sectors)
{
	if (unlikely(rdev->badblocks.count)) {
		int rv = badblocks_check(&rdev->badblocks, rdev->data_offset + s,
					sectors,
					first_bad, bad_sectors);
		if (rv)
			*first_bad -= rdev->data_offset;
		return rv;
	}
	return 0;
}
extern int rdev_set_badblocks(struct md_rdev *rdev, sector_t s, int sectors,
			      int is_new);
extern int rdev_clear_badblocks(struct md_rdev *rdev, sector_t s, int sectors,
				int is_new);
struct md_cluster_info;

/**
 * enum mddev_flags - md device flags.
 * @MD_ARRAY_FIRST_USE: First use of array, needs initialization.
 * @MD_CLOSING: If set, we are closing the array, do not open it then.
 * @MD_JOURNAL_CLEAN: A raid with journal is already clean.
 * @MD_HAS_JOURNAL: The raid array has journal feature set.
 * @MD_CLUSTER_RESYNC_LOCKED: cluster raid only, which means node, already took
 *			       resync lock, need to release the lock.
 * @MD_FAILFAST_SUPPORTED: Using MD_FAILFAST on metadata writes is supported as
 *			    calls to md_error() will never cause the array to
 *			    become failed.
 * @MD_HAS_PPL:  The raid array has PPL feature set.
 * @MD_HAS_MULTIPLE_PPLS: The raid array has multiple PPLs feature set.
 * @MD_ALLOW_SB_UPDATE: md_check_recovery is allowed to update the metadata
 *			 without taking reconfig_mutex.
 * @MD_UPDATING_SB: md_check_recovery is updating the metadata without
 *		     explicitly holding reconfig_mutex.
 * @MD_NOT_READY: do_md_run() is active, so 'array_state', ust not report that
 *		   array is ready yet.
 * @MD_BROKEN: This is used to stop writes and mark array as failed.
 * @MD_DELETED: This device is being deleted
 *
 * change UNSUPPORTED_MDDEV_FLAGS for each array type if new flag is added
 */
enum mddev_flags {
	MD_ARRAY_FIRST_USE,
	MD_CLOSING,
	MD_JOURNAL_CLEAN,
	MD_HAS_JOURNAL,
	MD_CLUSTER_RESYNC_LOCKED,
	MD_FAILFAST_SUPPORTED,
	MD_HAS_PPL,
	MD_HAS_MULTIPLE_PPLS,
	MD_ALLOW_SB_UPDATE,
	MD_UPDATING_SB,
	MD_NOT_READY,
	MD_BROKEN,
	MD_DELETED,
};

enum mddev_sb_flags {
	MD_SB_CHANGE_DEVS,		/* Some device status has changed */
	MD_SB_CHANGE_CLEAN,	/* transition to or from 'clean' */
	MD_SB_CHANGE_PENDING,	/* switch from 'clean' to 'active' in progress */
	MD_SB_NEED_REWRITE,	/* metadata write needs to be repeated */
};

#define NR_SERIAL_INFOS		8
/* record current range of serialize IOs */
struct serial_info {
	struct rb_node node;
	sector_t start;		/* start sector of rb node */
	sector_t last;		/* end sector of rb node */
	sector_t _subtree_last; /* highest sector in subtree of rb node */
};

/*
 * mddev->curr_resync stores the current sector of the resync but
 * also has some overloaded values.
 */
enum {
	/* No resync in progress */
	MD_RESYNC_NONE = 0,
	/* Yielded to allow another conflicting resync to commence */
	MD_RESYNC_YIELDED = 1,
	/* Delayed to check that there is no conflict with another sync */
	MD_RESYNC_DELAYED = 2,
	/* Any value greater than or equal to this is in an active resync */
	MD_RESYNC_ACTIVE = 3,
};

/*
 * md 设备通过块设备号和 struct block_device 关联起来，低层成员磁盘也指向和它相
 * 对应的 block_device ，正是以 block_device 为"纽带"，使得 md 可以构建在其它的
 * 物理或虚拟磁盘设备之上，成为一个"栈式"块设备
 */
struct mddev {
	/* 与 md personality 相关 */
	void				*private;
	struct md_personality		*pers;
	/* md 设备的设备号 */
	dev_t				unit;
	/* md 设备的次设备号 */
	int				md_minor;
	struct list_head		disks;
	unsigned long			flags;
	unsigned long			sb_flags;

	/* 如果为 1 ，表示 md 设备已经被挂起 */
	int				suspended;
	/* 计数器。发给 personality 处理前加 1 ，personality 处理结束后减 1 */
	struct percpu_ref		active_io;
	/*
	 * 取值为 0/1/2
	 * 0: 可写
	 * 1: 只读
	 * 2: 只读，但在第一次写时自动转换为可写(即将元数据标记为脏)
	 */
	int				ro;
	int				sysfs_active; /* set when sysfs deletes
						       * are happening, so run/
						       * takeover/stop are not safe
						       */
	/* 说明 md 设备是一个虚拟磁盘类设备 */
	struct gendisk			*gendisk;

	struct kobject			kobj;
	/*
	 * md 设备保持活动到什么时候。 UNTIL_IOCTL 保持到 IOCTL 结束， UNTIL_STOP
	 * 保持到 md 设备停止， 0 表示可以释放
	 */
	int				hold_active;
#define	UNTIL_IOCTL	1
#define	UNTIL_STOP	2

	/* Superblock information */
	int				major_version,
					minor_version,
					patch_version;
	/*
	 * 0: 表示 MD 设备的超级块只保存在内存中
	 * 1: 表示 MD 设备的超级块不仅在内存中，还保存在其成员磁盘中，以便在系统
	 *    重启之后能够读取超级块信息，重建 MD 设备。
	 */
	int				persistent;
	/* 为 1 表示元数据由外部管理，如用户空间 */
	int				external;	/* metadata is
							 * managed externally */
	/*
	 * 当 external 为 1 时有效。反映元数据的类型。 sysfs 文件系统中 metadata
	 * 属性文件的内容取决于 persistent,external,metadata_type 的值
	 */
	char				metadata_type[17]; /* externally set*/
	/*
	 * 以扇区为单位的 chunk 长度。 md 设备的数据被划分为多个 chunk(即条带模
	 * 型中的条带)，循环保存在成员磁盘上
	 */
	int				chunk_sectors;
	time64_t			ctime, utime;
	/*
	 * layout 是 md 设备的布局，仅适用某些 raid 个性，例如 raid5 的向左/向右
	 * 对称/不对称算法等
	 */
	int				level, layout;
	char				clevel[16];
	int				raid_disks;
	int				max_disks;
	/* 单个成员磁盘大小 */
	sector_t			dev_sectors;	/* used size of
							 * component devices */
	/* md 设备大小 */
	sector_t			array_sectors; /* exported array size */
	int				external_size; /* size managed
							* externally */
	/*
	 * md 设备的更新计数器，在创建 md 设备时清零，每次发生"重大事件"，如启动
	 * 阵列、停止队列、添加设备、备用盘激活等时递增 1 次。记录在 md 设备超级
	 * 块中，因此比较从各个成员磁盘读取的超级块的这个计数器可以知道哪个成员
	 * 磁盘更新
	 */
	__u64				events;
	/* If the last 'event' was simply a clean->dirty transition, and
	 * we didn't write it to the spares, then it is safe and simple
	 * to just decrement the event count on a dirty->clean transition.
	 * So we record that possibility here.
	 */
	int				can_decrease_events;

	/* md 设备的唯一标识符 */
	char				uuid[16];

	/* If the array is being reshaped, we need to record the
	 * new shape and an indication of where we are up to.
	 * This is written to the superblock.
	 * If reshape_position is MaxSector, then no reshape is happening (yet).
	 */
	/* 如果为 MaxSector ，表示没有进行 reshape ，或 reshape 已完成 */
	sector_t			reshape_position;
	/*
	 * 下面 5 个成员都是与 reshape 相关的。
	 * delta_disks: 对成员磁盘数目的改变
	 * new_level: 新的 raid 级别
	 * new_layout: 新的布局
	 */
	int				delta_disks, new_level, new_layout;
	/* 新的 chunk 长度，以扇区为单位 */
	int				new_chunk_sectors;
	int				reshape_backwards;

	/* 仅适用于某些 raid 个性，例如 raid5d 内核线程循环处理 stripe_head */
	struct md_thread __rcu		*thread;	/* management thread */
	/*
	 * 仅适用于某些 raid 个性，例如 resync 内核线程被用来处理同步、恢复、
	 * reshape 等
	 */
	struct md_thread __rcu		*sync_thread;	/* doing resync or reconstruct */

	/* 'last_sync_action' is initialized to "none".  It is set when a
	 * sync operation (i.e "data-check", "requested-resync", "resync",
	 * "recovery", or "reshape") is started.  It holds this value even
	 * when the sync thread is "frozen" (interrupted) or "idle" (stopped
	 * or finished).  It is overwritten when a new sync operation is begun.
	 */
	char				*last_sync_action;
	sector_t			curr_resync;	/* last block scheduled */
	/* As resync requests can complete out of order, we cannot easily track
	 * how much resync has been completed.  So we occasionally pause until
	 * everything completes, then set curr_resync_completed to curr_resync.
	 * As such it may be well behind the real resync mark, but it is a value
	 * we are certain of.
	 */
	sector_t			curr_resync_completed;
	unsigned long			resync_mark;	/* a recent timestamp */
	sector_t			resync_mark_cnt;/* blocks written at resync_mark */
	sector_t			curr_mark_cnt; /* blocks scheduled now */

	sector_t			resync_max_sectors; /* may be set by personality */

	atomic64_t			resync_mismatches; /* count of sectors where
							    * parity/replica mismatch found
							    */

	/* allow user-space to request suspension of IO to regions of the array */
	sector_t			suspend_lo;
	sector_t			suspend_hi;
	/* if zero, use the system-wide default */
	int				sync_speed_min;
	int				sync_speed_max;

	/* resync even though the same disks are shared among md-devices */
	int				parallel_resync;

	int				ok_start_degraded;

	unsigned long			recovery;
	/* If a RAID personality determines that recovery (of a particular
	 * device) will fail due to a read error on the source device, it
	 * takes a copy of this number and does not attempt recovery again
	 * until this number changes.
	 */
	int				recovery_disabled;

	int				in_sync;	/* know to not need resync */
	/* 'open_mutex' avoids races between 'md_open' and 'do_md_stop', so
	 * that we are never stopping an array while it is open.
	 * 'reconfig_mutex' protects all other reconfiguration.
	 * These locks are separate due to conflicting interactions
	 * with disk->open_mutex.
	 * Lock ordering is:
	 *  reconfig_mutex -> disk->open_mutex
	 *  disk->open_mutex -> open_mutex:  e.g. __blkdev_get -> md_open
	 */
	struct mutex			open_mutex;
	struct mutex			reconfig_mutex;
	atomic_t			active;		/* general refcount */
	atomic_t			openers;	/* number of active opens */

	int				changed;	/* True if we might need to
							 * reread partition info */
	int				degraded;	/* whether md should consider
							 * adding a spare
							 */

	atomic_t			recovery_active; /* blocks scheduled, but not written */
	wait_queue_head_t		recovery_wait;
	sector_t			recovery_cp;
	sector_t			resync_min;	/* user requested sync
							 * starts here */
	sector_t			resync_max;	/* resync should pause
							 * when it gets here */

	struct kernfs_node		*sysfs_state;	/* handle for 'array_state'
							 * file in sysfs.
							 */
	struct kernfs_node		*sysfs_action;  /* handle for 'sync_action' */
	struct kernfs_node		*sysfs_completed;	/*handle for 'sync_completed' */
	struct kernfs_node		*sysfs_degraded;	/*handle for 'degraded' */
	struct kernfs_node		*sysfs_level;		/*handle for 'level' */

	struct work_struct del_work;	/* used for delayed sysfs removal */

	/* "lock" protects:
	 *   flush_bio transition from NULL to !NULL
	 *   rdev superblocks, events
	 *   clearing MD_CHANGE_*
	 *   in_sync - and related safemode and MD_CHANGE changes
	 *   pers (also protected by reconfig_mutex and pending IO).
	 *   clearing ->bitmap
	 *   clearing ->bitmap_info.file
	 *   changing ->resync_{min,max}
	 *   setting MD_RECOVERY_RUNNING (which interacts with resync_{min,max})
	 */
	spinlock_t			lock;
	wait_queue_head_t		sb_wait;	/* for waiting on superblock updates */
	atomic_t			pending_writes;	/* number of active superblock writes */

	unsigned int			safemode;	/* if set, update "clean" superblock
							 * when no writes pending.
							 */
	unsigned int			safemode_delay;
	struct timer_list		safemode_timer;
	struct percpu_ref		writes_pending;
	int				sync_checkers;	/* # of threads checking writes_pending */
	/* 与 gendisk.queue 指向相同的 request queue */
	struct request_queue		*queue;	/* for plugging ... */

	struct bitmap			*bitmap; /* the bitmap for the device */
	struct {
		struct file		*file; /* the bitmap file */
		loff_t			offset; /* offset from superblock of
						 * start of bitmap. May be
						 * negative, but not '0'
						 * For external metadata, offset
						 * from start of device.
						 */
		unsigned long		space; /* space available at this offset */
		loff_t			default_offset; /* this is the offset to use when
							 * hot-adding a bitmap.  It should
							 * eventually be settable by sysfs.
							 */
		unsigned long		default_space; /* space available at
							* default offset */
		struct mutex		mutex;
		unsigned long		chunksize;
		unsigned long		daemon_sleep; /* how many jiffies between updates? */
		unsigned long		max_write_behind; /* write-behind mode */
		int			external;
		int			nodes; /* Maximum number of nodes in the cluster */
		char                    cluster_name[64]; /* Name of the cluster */
	} bitmap_info;

	atomic_t			max_corr_read_errors; /* max read retries */
	struct list_head		all_mddevs;

	const struct attribute_group	*to_remove;

	struct bio_set			bio_set;
	struct bio_set			sync_set; /* for sync operations like
						   * metadata and bitmap writes
						   */
	struct bio_set			io_clone_set;

	/* Generic flush handling.
	 * The last to finish preflush schedules a worker to submit
	 * the rest of the request (without the REQ_PREFLUSH flag).
	 */
	struct bio *flush_bio;
	atomic_t flush_pending;
	ktime_t start_flush, prev_flush_start; /* prev_flush_start is when the previous completed
						* flush was started.
						*/
	struct work_struct flush_work;
	struct work_struct event_work;	/* used by dm to report failure event */
	mempool_t *serial_info_pool;
	void (*sync_super)(struct mddev *mddev, struct md_rdev *rdev);
	struct md_cluster_info		*cluster_info;
	unsigned int			good_device_nr;	/* good device num within cluster raid */
	unsigned int			noio_flag; /* for memalloc scope API */

	/*
	 * Temporarily store rdev that will be finally removed when
	 * reconfig_mutex is unlocked, protected by reconfig_mutex.
	 */
	struct list_head		deleting;

	/* Used to synchronize idle and frozen for action_store() */
	struct mutex			sync_mutex;
	/* The sequence number for sync thread */
	atomic_t sync_seq;

	bool	has_superblocks:1;
	bool	fail_last_dev:1;
	bool	serialize_policy:1;
};

enum recovery_flags {
	/*
	 * If neither SYNC or RESHAPE are set, then it is a recovery.
	 */
	MD_RECOVERY_RUNNING,	/* a thread is running, or about to be started */
	MD_RECOVERY_SYNC,	/* actually doing a resync, not a recovery */
	MD_RECOVERY_RECOVER,	/* doing recovery, or need to try it. */
	MD_RECOVERY_INTR,	/* resync needs to be aborted for some reason */
	MD_RECOVERY_DONE,	/* thread is done and is waiting to be reaped */
	MD_RECOVERY_NEEDED,	/* we might need to start a resync/recover */
	MD_RECOVERY_REQUESTED,	/* user-space has requested a sync (used with SYNC) */
	MD_RECOVERY_CHECK,	/* user-space request for check-only, no repair */
	MD_RECOVERY_RESHAPE,	/* A reshape is happening */
	MD_RECOVERY_FROZEN,	/* User request to abort, and not restart, any action */
	MD_RECOVERY_ERROR,	/* sync-action interrupted because io-error */
	MD_RECOVERY_WAIT,	/* waiting for pers->start() to finish */
	MD_RESYNCING_REMOTE,	/* remote node is running resync thread */
};

enum md_ro_state {
	MD_RDWR,
	MD_RDONLY,
	MD_AUTO_READ,
	MD_MAX_STATE
};

static inline bool md_is_rdwr(struct mddev *mddev)
{
	return (mddev->ro == MD_RDWR);
}

static inline bool is_md_suspended(struct mddev *mddev)
{
	return percpu_ref_is_dying(&mddev->active_io);
}

static inline int __must_check mddev_lock(struct mddev *mddev)
{
	return mutex_lock_interruptible(&mddev->reconfig_mutex);
}

/* Sometimes we need to take the lock in a situation where
 * failure due to interrupts is not acceptable.
 */
static inline void mddev_lock_nointr(struct mddev *mddev)
{
	mutex_lock(&mddev->reconfig_mutex);
}

static inline int mddev_trylock(struct mddev *mddev)
{
	return mutex_trylock(&mddev->reconfig_mutex);
}
extern void mddev_unlock(struct mddev *mddev);

static inline void md_sync_acct(struct block_device *bdev, unsigned long nr_sectors)
{
	atomic_add(nr_sectors, &bdev->bd_disk->sync_io);
}

static inline void md_sync_acct_bio(struct bio *bio, unsigned long nr_sectors)
{
	md_sync_acct(bio->bi_bdev, nr_sectors);
}

struct md_personality
{
	char *name;
	int level;
	struct list_head list;
	struct module *owner;
	bool __must_check (*make_request)(struct mddev *mddev, struct bio *bio);
	/*
	 * start up works that do NOT require md_thread. tasks that
	 * requires md_thread should go into start()
	 */
	int (*run)(struct mddev *mddev);
	/* start up works that require md threads */
	int (*start)(struct mddev *mddev);
	void (*free)(struct mddev *mddev, void *priv);
	void (*status)(struct seq_file *seq, struct mddev *mddev);
	/* error_handler must set ->faulty and clear ->in_sync
	 * if appropriate, and should abort recovery if needed
	 */
	void (*error_handler)(struct mddev *mddev, struct md_rdev *rdev);
	int (*hot_add_disk) (struct mddev *mddev, struct md_rdev *rdev);
	int (*hot_remove_disk) (struct mddev *mddev, struct md_rdev *rdev);
	int (*spare_active) (struct mddev *mddev);
	/* 支持冗余特性的需要定义 */
	sector_t (*sync_request)(struct mddev *mddev, sector_t sector_nr, int *skipped);
	int (*resize) (struct mddev *mddev, sector_t sectors);
	sector_t (*size) (struct mddev *mddev, sector_t sectors, int raid_disks);
	int (*check_reshape) (struct mddev *mddev);
	int (*start_reshape) (struct mddev *mddev);
	void (*finish_reshape) (struct mddev *mddev);
	void (*update_reshape_pos) (struct mddev *mddev);
	void (*prepare_suspend) (struct mddev *mddev);
	/* quiesce suspends or resumes internal processing.
	 * 1 - stop new actions and wait for action io to complete
	 * 0 - return to normal behaviour
	 */
	void (*quiesce) (struct mddev *mddev, int quiesce);
	/* takeover is used to transition an array from one
	 * personality to another.  The new personality must be able
	 * to handle the data in the current layout.
	 * e.g. 2drive raid1 -> 2drive raid5
	 *      ndrive raid5 -> degraded n+1drive raid6 with special layout
	 * If the takeover succeeds, a new 'private' structure is returned.
	 * This needs to be installed and then ->run used to activate the
	 * array.
	 */
	void *(*takeover) (struct mddev *mddev);
	/* Changes the consistency policy of an active array. */
	int (*change_consistency_policy)(struct mddev *mddev, const char *buf);
};

struct md_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct mddev *, char *);
	ssize_t (*store)(struct mddev *, const char *, size_t);
};
extern const struct attribute_group md_bitmap_group;

static inline struct kernfs_node *sysfs_get_dirent_safe(struct kernfs_node *sd, char *name)
{
	if (sd)
		return sysfs_get_dirent(sd, name);
	return sd;
}
static inline void sysfs_notify_dirent_safe(struct kernfs_node *sd)
{
	if (sd)
		sysfs_notify_dirent(sd);
}

static inline char * mdname (struct mddev * mddev)
{
	return mddev->gendisk ? mddev->gendisk->disk_name : "mdX";
}

static inline int sysfs_link_rdev(struct mddev *mddev, struct md_rdev *rdev)
{
	char nm[20];
	if (!test_bit(Replacement, &rdev->flags) &&
	    !test_bit(Journal, &rdev->flags) &&
	    mddev->kobj.sd) {
		sprintf(nm, "rd%d", rdev->raid_disk);
		return sysfs_create_link(&mddev->kobj, &rdev->kobj, nm);
	} else
		return 0;
}

static inline void sysfs_unlink_rdev(struct mddev *mddev, struct md_rdev *rdev)
{
	char nm[20];
	if (!test_bit(Replacement, &rdev->flags) &&
	    !test_bit(Journal, &rdev->flags) &&
	    mddev->kobj.sd) {
		sprintf(nm, "rd%d", rdev->raid_disk);
		sysfs_remove_link(&mddev->kobj, nm);
	}
}

/*
 * iterates through some rdev ringlist. It's safe to remove the
 * current 'rdev'. Dont touch 'tmp' though.
 */
#define rdev_for_each_list(rdev, tmp, head)				\
	list_for_each_entry_safe(rdev, tmp, head, same_set)

/*
 * iterates through the 'same array disks' ringlist
 */
#define rdev_for_each(rdev, mddev)				\
	list_for_each_entry(rdev, &((mddev)->disks), same_set)

#define rdev_for_each_safe(rdev, tmp, mddev)				\
	list_for_each_entry_safe(rdev, tmp, &((mddev)->disks), same_set)

#define rdev_for_each_rcu(rdev, mddev)				\
	list_for_each_entry_rcu(rdev, &((mddev)->disks), same_set)

struct md_thread {
	void			(*run) (struct md_thread *thread);
	struct mddev		*mddev;
	wait_queue_head_t	wqueue;
	unsigned long		flags;
	struct task_struct	*tsk;
	unsigned long		timeout;
	void			*private;
};

struct md_io_clone {
	struct mddev	*mddev;
	struct bio	*orig_bio;
	unsigned long	start_time;
	struct bio	bio_clone;
};

#define THREAD_WAKEUP  0

static inline void safe_put_page(struct page *p)
{
	if (p) put_page(p);
}

extern int register_md_personality(struct md_personality *p);
extern int unregister_md_personality(struct md_personality *p);
extern int register_md_cluster_operations(struct md_cluster_operations *ops,
		struct module *module);
extern int unregister_md_cluster_operations(void);
extern int md_setup_cluster(struct mddev *mddev, int nodes);
extern void md_cluster_stop(struct mddev *mddev);
extern struct md_thread *md_register_thread(
	void (*run)(struct md_thread *thread),
	struct mddev *mddev,
	const char *name);
extern void md_unregister_thread(struct mddev *mddev, struct md_thread __rcu **threadp);
extern void md_wakeup_thread(struct md_thread __rcu *thread);
extern void md_check_recovery(struct mddev *mddev);
extern void md_reap_sync_thread(struct mddev *mddev);
extern int mddev_init_writes_pending(struct mddev *mddev);
extern bool md_write_start(struct mddev *mddev, struct bio *bi);
extern void md_write_inc(struct mddev *mddev, struct bio *bi);
extern void md_write_end(struct mddev *mddev);
extern void md_done_sync(struct mddev *mddev, int blocks, int ok);
extern void md_error(struct mddev *mddev, struct md_rdev *rdev);
extern void md_finish_reshape(struct mddev *mddev);
void md_submit_discard_bio(struct mddev *mddev, struct md_rdev *rdev,
			struct bio *bio, sector_t start, sector_t size);
void md_account_bio(struct mddev *mddev, struct bio **bio);

extern bool __must_check md_flush_request(struct mddev *mddev, struct bio *bio);
extern void md_super_write(struct mddev *mddev, struct md_rdev *rdev,
			   sector_t sector, int size, struct page *page);
extern int md_super_wait(struct mddev *mddev);
extern int sync_page_io(struct md_rdev *rdev, sector_t sector, int size,
		struct page *page, blk_opf_t opf, bool metadata_op);
extern void md_do_sync(struct md_thread *thread);
extern void md_new_event(void);
extern void md_allow_write(struct mddev *mddev);
extern void md_wait_for_blocked_rdev(struct md_rdev *rdev, struct mddev *mddev);
extern void md_set_array_sectors(struct mddev *mddev, sector_t array_sectors);
extern int md_check_no_bitmap(struct mddev *mddev);
extern int md_integrity_register(struct mddev *mddev);
extern int md_integrity_add_rdev(struct md_rdev *rdev, struct mddev *mddev);
extern int strict_strtoul_scaled(const char *cp, unsigned long *res, int scale);

extern void mddev_init(struct mddev *mddev);
struct mddev *md_alloc(dev_t dev, char *name);
void mddev_put(struct mddev *mddev);
extern int md_run(struct mddev *mddev);
extern int md_start(struct mddev *mddev);
extern void md_stop(struct mddev *mddev);
extern void md_stop_writes(struct mddev *mddev);
extern int md_rdev_init(struct md_rdev *rdev);
extern void md_rdev_clear(struct md_rdev *rdev);

extern void md_handle_request(struct mddev *mddev, struct bio *bio);
extern void mddev_suspend(struct mddev *mddev);
extern void mddev_resume(struct mddev *mddev);

extern void md_reload_sb(struct mddev *mddev, int raid_disk);
extern void md_update_sb(struct mddev *mddev, int force);
extern void mddev_create_serial_pool(struct mddev *mddev, struct md_rdev *rdev,
				     bool is_suspend);
extern void mddev_destroy_serial_pool(struct mddev *mddev, struct md_rdev *rdev,
				      bool is_suspend);
struct md_rdev *md_find_rdev_nr_rcu(struct mddev *mddev, int nr);
struct md_rdev *md_find_rdev_rcu(struct mddev *mddev, dev_t dev);

static inline bool is_rdev_broken(struct md_rdev *rdev)
{
	return !disk_live(rdev->bdev->bd_disk);
}

static inline void rdev_dec_pending(struct md_rdev *rdev, struct mddev *mddev)
{
	int faulty = test_bit(Faulty, &rdev->flags);
	if (atomic_dec_and_test(&rdev->nr_pending) && faulty) {
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
	}
}

extern struct md_cluster_operations *md_cluster_ops;
static inline int mddev_is_clustered(struct mddev *mddev)
{
	return mddev->cluster_info && mddev->bitmap_info.nodes > 1;
}

/* clear unsupported mddev_flags */
static inline void mddev_clear_unsupported_flags(struct mddev *mddev,
	unsigned long unsupported_flags)
{
	mddev->flags &= ~unsupported_flags;
}

static inline void mddev_check_write_zeroes(struct mddev *mddev, struct bio *bio)
{
	if (bio_op(bio) == REQ_OP_WRITE_ZEROES &&
	    !bio->bi_bdev->bd_disk->queue->limits.max_write_zeroes_sectors)
		mddev->queue->limits.max_write_zeroes_sectors = 0;
}

struct mdu_array_info_s;
struct mdu_disk_info_s;

extern int mdp_major;
extern struct workqueue_struct *md_bitmap_wq;
void md_autostart_arrays(int part);
int md_set_array_info(struct mddev *mddev, struct mdu_array_info_s *info);
int md_add_new_disk(struct mddev *mddev, struct mdu_disk_info_s *info);
int do_md_run(struct mddev *mddev);

extern const struct block_device_operations md_fops;

#endif /* _MD_MD_H */
