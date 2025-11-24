#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

// wakeup_proc - 唤醒进程
// 把进程状态设置为 PROC_RUNNABLE，使其可以被调度器选中
void
wakeup_proc(struct proc_struct *proc) {
    // 只有当进程处于“睡眠”或“未初始化”等合理状态时才能被唤醒
    // 不能唤醒僵尸进程或已经在运行的进程
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

// schedule - 简单的 FIFO (先进先出) / 轮询调度器
void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;
    
    // 1. 关中断
    // 调度过程涉及访问和修改全局进程链表，必须保证原子性，防止被再次中断打断
    local_intr_save(intr_flag);
    {
        // 清除当前进程的“需要调度”标记
        current->need_resched = 0;
        
        // 确定遍历的起点和终点
        // 如果当前是 idleproc，从链表头开始找；否则从当前进程的下一个开始找 (实现轮询)
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        
        // 2. 遍历进程链表，寻找下一个可运行 (RUNNABLE) 的进程
        do {
            if ((le = list_next(le)) != &proc_list) {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE) {
                    break; // 找到了！跳出循环
                }
            }
            next = NULL; // 如果这个不是，重置 next，继续找
        } while (le != last); // 如果转了一圈回到起点，说明没找到其他可运行进程
        
        // 3. 如果没找到任何可运行进程，就运行 idleproc (兜底)
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }
        
        // 增加运行次数统计
        next->runs ++;
        
        // 4. 如果选出的进程不是当前进程，则执行切换
        if (next != current) {
            proc_run(next); // 调用 proc.c 中的函数进行上下文切换
        }
    }
    // 5. 恢复中断状态
    local_intr_restore(intr_flag);
}
