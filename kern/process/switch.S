#include <riscv.h>

.text
# void switch_to(struct context *from, struct context *to)
# 上下文切换函数
# a0: 指向当前进程 (from) 的上下文结构体 (struct context) 的指针
# a1: 指向下一个进程 (to) 的上下文结构体 (struct context) 的指针
.globl switch_to
switch_to:
    # --- 保存当前进程 (from) 的上下文 ---
    # 将 CPU 的 callee-saved (被调用者保存) 寄存器保存到 from->context 中
    
    # REGBYTES 是寄存器宽度 (64位系统为 8 字节)
    STORE ra, 0*REGBYTES(a0)   # 保存返回地址 (Return Address)
                               # 这是 switch_to 返回后下一条指令的地址
    STORE sp, 1*REGBYTES(a0)   # 保存栈指针 (Stack Pointer)
    STORE s0, 2*REGBYTES(a0)   # 保存 s0-s11 (被调用者保存寄存器)
    STORE s1, 3*REGBYTES(a0)
    STORE s2, 4*REGBYTES(a0)
    STORE s3, 5*REGBYTES(a0)
    STORE s4, 6*REGBYTES(a0)
    STORE s5, 7*REGBYTES(a0)
    STORE s6, 8*REGBYTES(a0)
    STORE s7, 9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)

    # --- 恢复下一个进程 (to) 的上下文 ---
    # 从 to->context 中读取数据加载到 CPU 寄存器中
    
    LOAD ra, 0*REGBYTES(a1)    # 恢复返回地址
                               # 对于新进程，这里存的是 forkret 的地址
                               # 对于暂停的旧进程，这里是它上次 switch_to 后面的地址
    LOAD sp, 1*REGBYTES(a1)    # 恢复栈指针 (切换到了新进程的内核栈！)
    LOAD s0, 2*REGBYTES(a1)    # 恢复 s0-s11
    LOAD s1, 3*REGBYTES(a1)
    LOAD s2, 4*REGBYTES(a1)
    LOAD s3, 5*REGBYTES(a1)
    LOAD s4, 6*REGBYTES(a1)
    LOAD s5, 7*REGBYTES(a1)
    LOAD s6, 8*REGBYTES(a1)
    LOAD s7, 9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)

    # 返回指令
    # 这实际上是跳转到 ra 寄存器指向的地址。
    # 由于我们刚刚从 to->context 中恢复了 ra，所以这条指令执行后，
    # CPU 将跳转到新进程的代码位置继续执行。
    ret