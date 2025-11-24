#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <kdebug.h>
#include <picirq.h>
#include <trap.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <vmm.h>
#include <proc.h>
#include <kmonitor.h>
#include <dtb.h>

int kern_init(void) __attribute__((noreturn));
void grade_backtrace(void);

int kern_init(void)
{
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    dtb_init();
    cons_init(); // init the console

    const char *message = "(THU.CST) os is loading ...";
    cprintf("%s\n\n", message);

    print_kerninfo();

    // grade_backtrace();

    pmm_init();  // 1. 初始化物理内存
                 // 没有它，你没法在 proc_init 里分配 PCB (kmalloc)

    pic_init();  // 初始化中断控制器
    idt_init();  // 2. 初始化中断向量表
                 // 告诉 CPU：发生中断要去 trapentry.S

    vmm_init();  // 3. 初始化虚拟内存管理
                 // 初始化 vma 相关结构，为进程的虚拟地址空间做准备

    proc_init(); //  4. 进程子系统初始化
                 // 这里会创建 idleproc (0号) 和 initproc (1号)
                 // 此时 initproc 已经处于 "就绪(RUNNABLE)" 状态，但还没开始跑。

    clock_init();  // 5. 开启时钟计数
    intr_enable(); // 6. 开启全局中断 (开闸放水)
                   // 这一行执行后，时钟中断开始 tick。
                   // 一旦 tick 发生，trap.c 会调用 schedule()，
                   // 调度器发现有 initproc 在排队，就会切换过去。

    cpu_idle(); // 7. 进入 idle 循环
                // 这是一个死循环。如果系统空闲，CPU 就呆在这里。
                // 如果有其他线程（如 initproc），schedule() 会把 CPU 切走。
}

static void
lab1_print_cur_status(void)
{
    static int round = 0;
    round++;
}
