#include <riscv.h>

    /*
     * 宏定义: SAVE_ALL
     * 作用: 保存所有的通用寄存器和部分 CSR 寄存器到当前内核栈中，
     *       构建一个 struct trapframe 结构体。
     */
    .macro SAVE_ALL
    .align 2

    /* 
     * 交换/保存 SP 到 sscratch。
     * 在内核态发生中断时，sp 指向内核栈。
     * 我们先把它存到 sscratch 中，稍后读取出来保存到 trapframe 中。
     */
    csrw sscratch, sp

    /* 
     * 分配栈空间
     * 36 * 8 bytes = 288 bytes，正好是 struct trapframe 的大小
     */
    addi sp, sp, -36 * REGBYTES

    /* --- 保存通用寄存器 (GPRs) --- */
    /* x0 (zero) 不需要保存，因为它恒为 0 */
    /* x2 (sp) 后面单独处理 */
    STORE  x1,1*REGBYTES(sp)   // ra
    STORE  x3,3*REGBYTES(sp)   // gp
    STORE  x4,4*REGBYTES(sp)   // tp
    STORE  x5,5*REGBYTES(sp)   // t0
    STORE  x6,6*REGBYTES(sp)   // t1
    STORE  x7,7*REGBYTES(sp)   // t2
    STORE  x8,8*REGBYTES(sp)   // s0/fp
    STORE  x9,9*REGBYTES(sp)   // s1
    STORE  x10,10*REGBYTES(sp) // a0
    STORE  x11,11*REGBYTES(sp) // a1
    STORE  x12,12*REGBYTES(sp) // a2
    STORE  x13,13*REGBYTES(sp) // a3
    STORE  x14,14*REGBYTES(sp) // a4
    STORE  x15,15*REGBYTES(sp) // a5
    STORE  x16,16*REGBYTES(sp) // a6
    STORE  x17,17*REGBYTES(sp) // a7
    STORE  x18,18*REGBYTES(sp) // s2
    STORE  x19,19*REGBYTES(sp) // s3
    STORE  x20,20*REGBYTES(sp) // s4
    STORE  x21,21*REGBYTES(sp) // s5
    STORE  x22,22*REGBYTES(sp) // s6
    STORE  x23,23*REGBYTES(sp) // s7
    STORE  x24,24*REGBYTES(sp) // s8
    STORE  x25,25*REGBYTES(sp) // s9
    STORE  x26,26*REGBYTES(sp) // s10
    STORE  x27,27*REGBYTES(sp) // s11
    STORE  x28,28*REGBYTES(sp) // t3
    STORE  x29,29*REGBYTES(sp) // t4
    STORE  x30,30*REGBYTES(sp) // t5
    STORE  x31,31*REGBYTES(sp) // t6

    /* --- 获取并保存 CSR 寄存器 --- */
    /* 
     * 将 CSR 读取到临时通用寄存器 s0-s4 中，然后保存到栈上
     * 注意：这里的 s0-s4 是临时借用的，它们原本的值已经在上面被保存了
     */
    csrr s0, sscratch   // 读取原始的 sp (中断发生前的值)
    csrr s1, sstatus    // 读取 sstatus (状态寄存器，包含中断使能位等)
    csrr s2, sepc       // 读取 sepc (异常程序计数器，记录中断发生时的指令地址)
    csrr s3, 0x143      // 读取 stval (0x143 是 stval/sbadaddr 的地址，记录出错的虚拟地址)
    csrr s4, scause     // 读取 scause (记录中断/异常的原因)

    /* 将 CSR 的值保存到 trapframe 对应的位置 */
    STORE s0, 2*REGBYTES(sp)   // 保存原始 sp 到 trapframe->gpr.sp
    STORE s1, 32*REGBYTES(sp)  // 保存 sstatus 到 trapframe->status
    STORE s2, 33*REGBYTES(sp)  // 保存 sepc 到 trapframe->epc
    STORE s3, 34*REGBYTES(sp)  // 保存 stval 到 trapframe->badvaddr
    STORE s4, 35*REGBYTES(sp)  // 保存 scause 到 trapframe->cause
    .endm

    /*
     * 宏定义: RESTORE_ALL
     * 作用: 从当前栈顶的 trapframe 结构体中恢复寄存器状态，
     *       为退出中断做准备。
     */
    .macro RESTORE_ALL

    /* 恢复 CSR 寄存器 */
    LOAD s1, 32*REGBYTES(sp) // 加载 sstatus
    LOAD s2, 33*REGBYTES(sp) // 加载 sepc

    /* 
     * 注意：stval (s3) 和 scause (s4) 不需要恢复，
     * 因为它们是只读的或者只记录异常原因，返回后不再需要。
     */
    csrw sstatus, s1         // 写入 sstatus (恢复中断使能状态、特权级等)
    csrw sepc, s2            // 写入 sepc (设置返回地址)

    /* 恢复通用寄存器 (x registers) */
    LOAD  x1,1*REGBYTES(sp)
    LOAD  x3,3*REGBYTES(sp)
    LOAD  x4,4*REGBYTES(sp)
    LOAD  x5,5*REGBYTES(sp)
    LOAD  x6,6*REGBYTES(sp)
    LOAD  x7,7*REGBYTES(sp)
    LOAD  x8,8*REGBYTES(sp)
    LOAD  x9,9*REGBYTES(sp)
    LOAD  x10,10*REGBYTES(sp)
    LOAD  x11,11*REGBYTES(sp)
    LOAD  x12,12*REGBYTES(sp)
    LOAD  x13,13*REGBYTES(sp)
    LOAD  x14,14*REGBYTES(sp)
    LOAD  x15,15*REGBYTES(sp)
    LOAD  x16,16*REGBYTES(sp)
    LOAD  x17,17*REGBYTES(sp)
    LOAD  x18,18*REGBYTES(sp)
    LOAD  x19,19*REGBYTES(sp)
    LOAD  x20,20*REGBYTES(sp)
    LOAD  x21,21*REGBYTES(sp)
    LOAD  x22,22*REGBYTES(sp)
    LOAD  x23,23*REGBYTES(sp)
    LOAD  x24,24*REGBYTES(sp)
    LOAD  x25,25*REGBYTES(sp)
    LOAD  x26,26*REGBYTES(sp)
    LOAD  x27,27*REGBYTES(sp)
    LOAD  x28,28*REGBYTES(sp)
    LOAD  x29,29*REGBYTES(sp)
    LOAD  x30,30*REGBYTES(sp)
    LOAD  x31,31*REGBYTES(sp)
    
    /* 
     * 最后恢复栈指针 sp (x2)
     * 执行完这一条，栈指针就指回了中断发生前的位置，
     * 相当于释放了 trapframe 占用的栈空间。
     */
    LOAD  x2,2*REGBYTES(sp)
    .endm

    /*
     * 全局入口点: __alltraps
     * 所有中断和异常都会跳转到这里 (因为 stvec 指向这里)。
     */
    .globl __alltraps
__alltraps:
    SAVE_ALL       // 1. 保存现场

    move  a0, sp   // 2. 传递参数
                   // 按照 RISC-V 调用约定，第一个参数放在 a0。
                   // 这里将指向 trapframe 的 sp 传给 trap() 函数。
    
    jal trap       // 3. 调用 C 语言的中断处理函数 trap(struct trapframe *tf)
    
    // 当 trap 函数返回时，sp 应该和调用前一样（指向 trapframe）

    /*
     * 全局标号: __trapret
     * 中断返回路径。
     */
    .globl __trapret
__trapret:
    RESTORE_ALL    // 4. 恢复现场
    
    # go back from supervisor call
    sret           // 5. 特权级返回指令
                   // - 将 sepc 复制到 pc (跳转回中断点)
                   // - 恢复 sstatus 中的特权级和中断使能位

    /*
     * 全局标号: forkrets
     * 作用: 新创建进程的“假”中断返回入口。
     * 
     * 在 copy_thread 中，我们将新进程的 context.ra 设置为了 forkret (C函数)，
     * forkret 函数会调用 forkrets(current->tf)。
     * 
     * 所以当 switch_to 切换到新进程时：
     * 1. switch_to 返回跳转到 forkret (proc.c)
     * 2. forkret 调用 forkrets，并将 tf 作为参数传入 a0
     */
    .globl forkrets
forkrets:
    # set stack to this new process's trapframe
    move sp, a0    // 将 sp 指向传入的 trapframe (位于新进程的内核栈顶)
    j __trapret    // 跳转到 __trapret，执行 RESTORE_ALL 和 sret
                   // 这样就仿佛是从一次中断中返回一样，
                   // 最终跳转到 tf->epc (即 kernel_thread_entry) 开始执行。