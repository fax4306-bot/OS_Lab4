#ifndef __KERN_TRAP_TRAP_H__
#define __KERN_TRAP_TRAP_H__

#include <defs.h>

// 保存通用寄存器的结构体
// 对应汇编宏 SAVE_ALL 中保存到栈上的顺序
struct pushregs
{
    uintptr_t zero; // 硬连线为 0 的寄存器 (x0)
    uintptr_t ra;   // 返回地址 (Return Address, x1) - Caller Saved
    uintptr_t sp;   // 栈指针 (Stack Pointer, x2) - Callee Saved
    uintptr_t gp;   // 全局指针 (Global Pointer, x3)
    uintptr_t tp;   // 线程指针 (Thread Pointer, x4)
    uintptr_t t0;   // 临时寄存器 (Temporary, x5) - Caller Saved
    uintptr_t t1;   // 临时寄存器 (x6) - Caller Saved
    uintptr_t t2;   // 临时寄存器 (x7) - Caller Saved
    uintptr_t s0;   // 保存寄存器/帧指针 (Saved/Frame Pointer, x8) - Callee Saved
    uintptr_t s1;   // 保存寄存器 (x9) - Callee Saved
    uintptr_t a0;   // 函数参数/返回值 (x10) - Caller Saved
    uintptr_t a1;   // 函数参数/返回值 (x11) - Caller Saved
    uintptr_t a2;   // 函数参数 (x12) - Caller Saved
    uintptr_t a3;   // 函数参数 (x13) - Caller Saved
    uintptr_t a4;   // 函数参数 (x14) - Caller Saved
    uintptr_t a5;   // 函数参数 (x15) - Caller Saved
    uintptr_t a6;   // 函数参数 (x16) - Caller Saved
    uintptr_t a7;   // 函数参数 (x17) - Caller Saved
    uintptr_t s2;   // 保存寄存器 (x18) - Callee Saved
    uintptr_t s3;   // 保存寄存器 (x19) - Callee Saved
    uintptr_t s4;   // 保存寄存器 (x20) - Callee Saved
    uintptr_t s5;   // 保存寄存器 (x21) - Callee Saved
    uintptr_t s6;   // 保存寄存器 (x22) - Callee Saved
    uintptr_t s7;   // 保存寄存器 (x23) - Callee Saved
    uintptr_t s8;   // 保存寄存器 (x24) - Callee Saved
    uintptr_t s9;   // 保存寄存器 (x25) - Callee Saved
    uintptr_t s10;  // 保存寄存器 (x26) - Callee Saved
    uintptr_t s11;  // 保存寄存器 (x27) - Callee Saved
    uintptr_t t3;   // 临时寄存器 (x28) - Caller Saved
    uintptr_t t4;   // 临时寄存器 (x29) - Caller Saved
    uintptr_t t5;   // 临时寄存器 (x30) - Caller Saved
    uintptr_t t6;   // 临时寄存器 (x31) - Caller Saved
};

// 中断帧 (TrapFrame)
// 记录中断/异常发生时的 CPU 现场（上下文）
// 它包含所有的通用寄存器以及几个关键的 CSR (控制状态寄存器)
struct trapframe
{
    struct pushregs gpr; // 通用寄存器组
    uintptr_t status;    // sstatus (Supervisor Status) 寄存器：记录中断使能位、特权级等
    uintptr_t epc;       // sepc (Supervisor Exception Program Counter) 寄存器：记录触发异常的指令地址
    uintptr_t badvaddr;  // stval (Supervisor Trap Value) 寄存器：记录导致异常的内存地址（如缺页地址）
    uintptr_t cause;     // scause (Supervisor Cause) 寄存器：记录中断或异常的原因
};

void trap(struct trapframe *tf); // 中断处理的总入口函数
void idt_init(void);             // 初始化中断描述符表 (实际上是初始化 stvec)
void print_trapframe(struct trapframe *tf); // 打印中断帧信息 (调试用)
void print_regs(struct pushregs *gpr);      // 打印通用寄存器 (调试用)

#endif /* !__KERN_TRAP_TRAP_H__ */