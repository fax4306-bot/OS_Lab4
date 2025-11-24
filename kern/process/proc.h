#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>

// process's state in his life cycle
// 进程在其生命周期中的状态
enum proc_state
{
    PROC_UNINIT = 0, // uninitialized - 未初始化：刚被 alloc_proc 分配，但尚未初始化
    PROC_SLEEPING,   // sleeping      - 睡眠/阻塞：等待某些事件（如锁、I/O、子进程退出）
    PROC_RUNNABLE,   // runnable      - 就绪/运行：正在运行或准备好被调度运行
    PROC_ZOMBIE,     // zombie        - 僵尸：进程已退出，但其资源尚未被父进程回收
};

// 保存进程切换时的上下文（Context）
// 这些是 RISC-V 调用约定中规定由“被调用者保存 (Callee-Saved)”的寄存器。
// 当调用 switch_to 进行进程切换时，当前进程的这些寄存器会被保存到这里。
// 至于 caller-saved 寄存器，编译器会自动在调用 switch_to 前保存到栈上。
struct context
{
    uintptr_t ra;  // Return Address: 返回地址 (切换回来后继续执行的指令地址)
    uintptr_t sp;  // Stack Pointer: 栈指针 (切换回来后使用的内核栈顶)
    uintptr_t s0;  // Saved Register / Frame Pointer: 帧指针
    uintptr_t s1;  // Saved Register
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};

#define PROC_NAME_LEN 15           // 进程名的最大长度
#define MAX_PROCESS 4096           // 系统允许的最大进程数
#define MAX_PID (MAX_PROCESS * 2)  // 最大的 PID 值

extern list_entry_t proc_list;     // 所有进程控制块的双向链表

// 进程控制块 (Process Control Block, PCB)
struct proc_struct
{
    enum proc_state state;        // Process state: 进程当前状态
    int pid;                      // Process ID: 进程唯一标识符
    int runs;                     // the running times of Proces: 进程被调度的次数
    uintptr_t kstack;             // Process kernel stack: 进程的内核栈地址
                                  // 每个线程都有自己独立的内核栈，用于在内核态执行函数调用。
    volatile bool need_resched;   // bool value: need to be rescheduled to release CPU?
                                  // 调度标志位：如果为 1，表示该进程需要主动让出 CPU (被抢占)
    struct proc_struct *parent;   // the parent process: 父进程控制块指针
    struct mm_struct *mm;         // Process's memory management field: 内存管理结构体
                                  // 包含页表和 VMA 链表。内核线程此项为 NULL (共享内核内存)。
    struct context context;       // Switch here to run process: 进程上下文
                                  // 用于进程切换 (switch_to)，保存被调用者保存的寄存器。
    struct trapframe *tf;         // Trap frame for current interrupt: 当前中断帧
                                  // 当进程从用户态陷入内核态（或被中断打断）时，现场信息保存在这里。
                                  // 这里的 tf 指针通常指向内核栈的某个位置。
    uintptr_t pgdir;              // the base addr of Page Directroy Table(PDT): 页目录表物理基址
                                  // 对应 satp 寄存器的值。内核线程通常指向 boot_pgdir。
    uint32_t flags;               // Process flag: 进程标志位
    char name[PROC_NAME_LEN + 1]; // Process name: 进程名称
    list_entry_t list_link;       // Process link list: 进程链表节点 (链接到 proc_list)
    list_entry_t hash_link;       // Process hash list: 哈希表节点 (用于通过 pid 快速查找 proc)
};

// 将链表节点转换为 proc_struct 指针
#define le2proc(le, member) \
    to_struct((le), struct proc_struct, member)

// 几个关键的全局进程指针
extern struct proc_struct *idleproc; // 0号进程 (空闲进程，负责调度)
extern struct proc_struct *initproc; // 1号进程 (初始化进程，第一个内核线程)
extern struct proc_struct *current;  // 当前正在 CPU 上运行的进程

// 函数声明

void proc_init(void); // 初始化进程子系统
void proc_run(struct proc_struct *proc); // 调度并运行指定进程 (Lab4 Ex3)

// 创建内核线程
// fn: 线程要执行的函数
// arg: 传递给函数的参数
// clone_flags: 克隆标志
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name); // 设置进程名
char *get_proc_name(struct proc_struct *proc); // 获取进程名
void cpu_idle(void) __attribute__((noreturn)); // idle 进程的主循环

struct proc_struct *find_proc(int pid); // 根据 PID 查找进程
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf); // 创建新进程的核心函数 (Lab4 Ex2)
int do_exit(int error_code); // 进程退出函数

#endif /* !__KERN_PROCESS_PROC_H__ */