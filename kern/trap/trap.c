#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>
#include <vmm.h>
#include <sbi.h>
#include <proc.h>

#define TICK_NUM 100
#define PRINT_TICK_NUM 10

// 辅助函数：打印 tick 计数，用于验证时钟中断是否工作
static void print_ticks()
{
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}
extern void sbi_shutdown(void);
static size_t print_count=0;

/* idt_init - 初始化中断处理入口
 * 实际上是设置 stvec 寄存器，让 CPU 知道发生中断时跳到哪里去
 */
void idt_init(void)
{
    extern void __alltraps(void); // 声明汇编入口点，定义在 trapentry.S 中

    /* 设置 sscratch 寄存器为 0
     * 在 trapentry.S 中，我们会通过交换 sscratch 和 sp 来判断中断是来自内核态还是用户态。
     * 0 表示我们当前正在内核态执行。 */
    write_csr(sscratch, 0);

    /* 设置异常向量地址 (stvec)
     * 将 stvec 设置为 __alltraps 的地址。
     * 并且由于地址是 4 字节对齐的，低两位为 0，表示使用 Direct 模式 (所有中断跳到同一地址) */
    write_csr(stvec, &__alltraps);
    
    /* 允许内核访问用户内存 (设置 sstatus 的 SUM 位)
     * SSTATUS_SUM (Permit Supervisor User Memory access)
     * 如果不设置，内核态代码读取用户态指针会导致 Load Fault */
    set_csr(sstatus, SSTATUS_SUM);
}

// 打印 TrapFrame 信息，用于调试异常
void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

// 打印通用寄存器
void print_regs(struct pushregs *gpr)
{
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

extern struct mm_struct *check_mm_struct;

// 中断处理函数 (处理异步事件)
void interrupt_handler(struct trapframe *tf)
{
    // scause 最高位是 1，表示中断。这里去掉最高位得到中断号。
    intptr_t cause = (tf->cause << 1) >> 1;
    switch (cause)
    {
    case IRQ_U_SOFT:
        cprintf("User software interrupt\n");
        break;
    case IRQ_S_SOFT:
        cprintf("Supervisor software interrupt\n");
        break;
    case IRQ_H_SOFT:
        cprintf("Hypervisor software interrupt\n");
        break;
    case IRQ_M_SOFT:
        cprintf("Machine software interrupt\n");
        break;
    case IRQ_U_TIMER:
        cprintf("User software interrupt\n");
        break;
    case IRQ_S_TIMER: // S 态时钟中断
        // "sip 寄存器中除了 SSIP 和 USIP 外的位都是只读的。"
        // 调用 sbi_set_timer 会清除 STIP，或者我们可以直接清除它。
        
        /* LAB3 代码逻辑 (在 Lab4 中依然适用，但需要增加调度逻辑) */ 
        clock_set_next_event(); // 设置下一次时钟中断
        
        // ticks 计数器加 1 (在 clock_init 中已初始化为 0)
        ticks++;
        
        // 每 100 个 tick (约 1 秒) 打印一次
        if (ticks % TICK_NUM == 0) {
            print_ticks();
            print_count++;
        }
        
        // 打印 10 次后关机 (仅用于测试)
        if (print_count == PRINT_TICK_NUM) {
            sbi_shutdown();
        }

        //触发进程调度：
        current->need_resched = 1;
        

        break;
    case IRQ_H_TIMER:
        cprintf("Hypervisor software interrupt\n");
        break;
    case IRQ_M_TIMER:
        cprintf("Machine software interrupt\n");
        break;
    case IRQ_U_EXT:
        cprintf("User software interrupt\n");
        break;
    case IRQ_S_EXT:
        cprintf("Supervisor external interrupt\n");
        break;
    case IRQ_H_EXT:
        cprintf("Hypervisor software interrupt\n");
        break;
    case IRQ_M_EXT:
        cprintf("Machine software interrupt\n");
        break;
    default:
        print_trapframe(tf);
        break;
    }
}

// 异常处理函数 (处理同步事件)
void exception_handler(struct trapframe *tf)
{
    int ret;
    switch (tf->cause)
    {
    case CAUSE_MISALIGNED_FETCH:
        cprintf("Instruction address misaligned\n");
        break;
    case CAUSE_FETCH_ACCESS:
        cprintf("Instruction access fault\n");
        break;
    case CAUSE_ILLEGAL_INSTRUCTION:
        cprintf("Exception type: Illegal instruction\n");
        cprintf("Illegal instruction caught at 0x%016llx\n", tf->epc);
        
        // 读取出错指令的内容 (epc 指向该指令)
        uint16_t instruction = *(uint16_t *)tf->epc;
        // (uint16_t *)tf->epc
        // → 把 epc（指令地址）强制转换为指向 16 位整数的指针。
        // *(uint16_t *)tf->epc
        // → 取出从这个地址开始的 16 位（2 字节）数据。
        
        // RISC-V 压缩指令 (C-Extension) 只有 2 字节
        // 判读是否为压缩指令：如果指令低 2 位不是 11 (0x3)，则是 16 位压缩指令
        if ((instruction & 0x3) != 0x3) // 这是一个 16-bit (2字节) 的压缩指令
        {
            cprintf("16-bit (2字节) 的压缩指令\n");
            tf->epc += 2; // 跳过当前指令 (2字节)
        }
        else // 这是一个 32-bit (4字节) 的标准指令
        {
            cprintf(" 32-bit (4字节) 的标准指令\n");
            tf->epc += 4; // 跳过当前指令 (4字节)
        }
        break;
    case CAUSE_BREAKPOINT:
        // 断点异常的处理 (ebreak 指令)
        // 这里实现了一个具备状态观测和交互式控制的初级内核调试器。
        cprintf("Exception type: breakpoint\n");
        cprintf("ebreak caught at 0x%016lx\n", tf->epc);

        // 功能 1: 状态观测 - 侦察函数内部状态 (参数和栈)。
        cprintf("\n --- Current Function State ---\n");
        cprintf("   Arguments (a0-a1): 0x%lx, 0x%lx\n", tf->gpr.a0, tf->gpr.a1);
        cprintf("   Stack Snapshot (around sp=0x%lx):\n", tf->gpr.sp);
        uintptr_t *sp = (uintptr_t *)tf->gpr.sp;
        for (int i = 0; i < 4; i++) {
            cprintf("     sp+%d: 0x%016lx\n", i * 8, *(sp + i));
        }

        // 功能 2: 状态观测 - 侦察系统全局状态。
        cprintf("\n --- Global State ---\n");
        cprintf("   Current 'ticks' value: %d\n", ticks);

        // 功能 3: 交互式控制 - 等待用户输入 'c' 以继续。
        cprintf("\n >> Type 'c' and press Enter to continue...\n");
        int c;
        while ((c = cons_getc()) != 'c') {
            // 这是一个“忙等待”循环，会持续检查控制台输入，直到收到 'c'。
        }
        cprintf("  >> 'c' received. Resuming execution.\n");

        // 功能 4: 健壮地恢复执行流。
        // 从 epc 指向的地址读取指令的前 16 位。
        instruction = *(uint16_t *)tf->epc;
        // 通过检查指令编码的最低两位来动态判断指令长度，以确保能正确跳过 ebreak 指令。
        if ((instruction & 0x3) != 0x3) {
            // 这是一个 16-bit 的压缩指令。
            tf->epc += 2;
        } else {
            // 这是一个 32-bit 的标准指令。
            tf->epc += 4;
        }
        cprintf("restore at 0x%016lx\n", tf->epc);
        break;
    case CAUSE_MISALIGNED_LOAD:
        cprintf("Load address misaligned\n");
        break;
    case CAUSE_LOAD_ACCESS:
        cprintf("Load access fault\n");

        break;
    case CAUSE_MISALIGNED_STORE:
        cprintf("AMO address misaligned\n");
        break;
    case CAUSE_STORE_ACCESS:
        cprintf("Store/AMO access fault\n");
        break;
    case CAUSE_USER_ECALL:
        cprintf("Environment call from U-mode\n");
        break;
    case CAUSE_SUPERVISOR_ECALL:
        cprintf("Environment call from S-mode\n");
        break;
    case CAUSE_HYPERVISOR_ECALL:
        cprintf("Environment call from H-mode\n");
        break;
    case CAUSE_MACHINE_ECALL:
        cprintf("Environment call from M-mode\n");
        break;
    case CAUSE_FETCH_PAGE_FAULT:
        cprintf("Instruction page fault\n"); // 指令缺页
        break;
    case CAUSE_LOAD_PAGE_FAULT:
        cprintf("Load page fault\n"); // 读取数据缺页
        break;
    case CAUSE_STORE_PAGE_FAULT:
        cprintf("Store/AMO page fault\n"); // 写入数据缺页
        break;
    default:
        print_trapframe(tf);
        break;
    }
}

/* *
 * trap - 处理或分发异常/中断。
 * 当 trap() 返回时，kern/trap/trapentry.S 中的代码会恢复 trapframe 中保存的旧 CPU 状态，
 * 然后使用 sret 指令从异常中返回。
 * */
void trap(struct trapframe *tf)
{
    // 根据 trap 的类型进行分发
    // scause < 0 (最高位为 1) 表示中断，否则为异常
    if ((intptr_t)tf->cause < 0)
    {
        // 中断处理 (Interrupts)
        interrupt_handler(tf);
    }
    else
    {
        // 异常处理 (Exceptions)
        exception_handler(tf);
    }
}