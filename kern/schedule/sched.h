#ifndef __KERN_SCHEDULE_SCHED_H__
#define __KERN_SCHEDULE_SCHED_H__

#include <proc.h>

// 触发调度，选择下一个进程并进行切换
void schedule(void);

// 唤醒一个进程（将其状态设置为 PROC_RUNNABLE）
void wakeup_proc(struct proc_struct *proc);

#endif /* !__KERN_SCHEDULE_SCHED_H__ */