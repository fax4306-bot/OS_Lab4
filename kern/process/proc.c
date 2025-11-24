#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <riscv.h>

/* ------------- 进程/线程机制的设计与实现 -------------
(一个简化的 Linux 进程/线程机制)

简介:
  uCore 实现了一个简单的进程/线程机制。进程包含独立的内存空间，至少一个用于执行的线程，
  内核数据（用于管理），处理器状态（用于上下文切换），文件（在 lab6 中）等。
  uCore 需要高效地管理所有这些细节。
  在 uCore 中，内核线程只是一种特殊的进程（它与其他进程共享内核内存空间）。

------------------------------
进程状态        :     含义                    -- 原因
    PROC_UNINIT     :   未初始化                -- alloc_proc (刚分配)
    PROC_SLEEPING   :   睡眠中                  -- try_free_pages, do_wait, do_sleep (等待资源或事件)
    PROC_RUNNABLE   :   可运行(可能正在运行)     -- proc_init, wakeup_proc (就绪态)
    PROC_ZOMBIE     :   僵尸状态(濒死)          -- do_exit (退出但未被父进程回收)

-----------------------------
进程状态变迁:

  alloc_proc                                 RUNNING (运行中)
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
进程关系
parent:           proc->parent  (proc 是 children)
children:         proc->cptr    (proc 是 parent) [注: uCore简化版可能没有维护子进程链表指针]
older sibling:    proc->optr    (proc 是 younger sibling)
younger sibling:  proc->yptr    (proc 是 older sibling)
-----------------------------
与进程相关的系统调用 (Syscall):
SYS_exit        : 进程退出                                --> do_exit
SYS_fork        : 创建子进程，复制 mm (内存管理结构)         --> do_fork-->wakeup_proc
SYS_wait        : 等待进程                                --> do_wait
SYS_exec        : fork 后，进程执行一个新程序               --> 加载程序并刷新 mm
SYS_clone       : 创建子线程                                --> do_fork-->wakeup_proc
SYS_yield       : 进程主动标记自己需要重新调度               --> proc->need_sched=1, 调度器将重新调度此进程
SYS_sleep       : 进程睡眠                                --> do_sleep
SYS_kill        : 杀掉进程                                --> do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : 获取进程 PID

*/

// 进程集合的链表 (所有进程都在这个链表中)
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// 基于 pid 的进程哈希列表 (用于快速查找)
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc (0号进程，空闲进程)
struct proc_struct *idleproc = NULL;
// init proc (1号进程，初始化进程)
struct proc_struct *initproc = NULL;
// current proc (当前占用 CPU 的进程)
struct proc_struct *current = NULL;

static int nr_process = 0; // 进程总数

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// alloc_proc - 分配一个 proc_struct 并初始化所有字段
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        // LAB4:EXERCISE1 YOUR CODE
        /*
         * 以下 proc_struct 中的字段需要被初始化：
         *       enum proc_state state;                      // 进程状态
         *       int pid;                                    // 进程 ID
         *       int runs;                                   // 进程运行次数
         *       uintptr_t kstack;                           // 进程内核栈
         *       volatile bool need_resched;                 // 是否需要重新调度？
         *       struct proc_struct *parent;                 // 父进程
         *       struct mm_struct *mm;                       // 进程的内存管理字段
         *       struct context context;                     // 运行进程的上下文
         *       struct trapframe *tf;                       // 当前中断的中断帧
         *       uintptr_t pgdir;                            // 页目录表(PDT)基址
         *       uint32_t flags;                             // 进程标志
         *       char name[PROC_NAME_LEN + 1];               // 进程名称
         */
        proc->state = PROC_UNINIT;  // 刚分配，状态为未初始化
        proc->pid = -1;             // -1 表示还没有分配具体的 PID
        proc->runs = 0;             // 还没运行过，为 0
        proc->kstack = 0;           // 内核栈还没分配，先置 0
        proc->need_resched = 0;     // 不需要调度
        proc->parent = NULL;        // 没有父进程
        proc->mm = NULL;            // 内存管理结构暂时为空（内核线程通常共享内核 mm，或者之后才分配）
        
        // context 是一个结构体，里面全是寄存器，必须清零
        memset(&(proc->context), 0, sizeof(struct context));
        
        proc->tf = NULL;            // 中断帧指针置空
        proc->pgdir = boot_pgdir_pa;// 页表基址赋值为内核页表基址，默认共享内核页表，用户进程会在do fork中覆盖
        proc->flags = 0;            // 标志位清零
        
        // 名字清零
        memset(proc->name, 0, PROC_NAME_LEN + 1);

    }
    return proc;
}

// set_proc_name - 设置进程名
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - 获取进程名
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// get_pid - 为进程分配一个唯一的 pid
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    
    // 简单的线性搜索算法来寻找未使用的 PID
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}

// proc_run - 让进程 "proc" 在 cpu 上运行
// 注意: 在调用 switch_to 之前，应该加载 "proc" 的新 PDT 基址
void proc_run(struct proc_struct *proc)
{
    if (proc != current)
    {
        // LAB4:EXERCISE3 YOUR CODE
        /*
         * 一些有用的宏、函数和定义：
         *   local_intr_save():        禁用中断 (保护临界区)
         *   local_intr_restore():     开启中断
         *   lsatp():                  修改 satp 寄存器的值 (切换页表)
         *                             (实际使用 lsatp(proc->pgdir) 或类似机制)
         *   switch_to():              在两个进程之间进行上下文切换
         */

        bool intr_flag;
        struct proc_struct *prev = current, *next = proc;
        // 1. 关中断：保证切换过程原子性，不被时钟中断打断
        local_intr_save(intr_flag);
        {
            // 软件层面
            // 2. 切换当前进程指针
            current = proc;
            // 硬件层面
            // 3. 切换页表，这里用到 lsatp
            lsatp(next->pgdir); 
            // 4. 刷新 TLB
            flush_tlb();
            // 5. 切换上下文
            switch_to(&(prev->context), &(next->context));
        }
        // 6. 恢复中断
        // 这行代码是在 switch_to 返回后执行的，也就是当 CPU 再次切换回这个进程时，才会执行这一行。
        local_intr_restore(intr_flag);
    }
}

// forkret -- 新线程/进程的第一个内核入口点
// 注意: forkret 的地址是在 copy_thread 函数中设置的 (context.ra)
//       在 switch_to 之后，当前进程将从这里开始执行。
static void
forkret(void)
{
    forkrets(current->tf); // 跳转到 entry.S 中的 forkrets
}

// hash_proc - 将 proc 加入到进程哈希列表中
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - 根据 pid 从进程哈希列表中查找 proc
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

// kernel_thread - 使用 "fn" 函数创建一个内核线程
// 注意: 临时 trapframe tf 的内容将在 do_fork-->copy_thread 函数中
//       被复制到 proc->tf
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    
    // 设置内核线程的参数和入口
    // s0 保存函数指针，s1 保存参数 (参见 entry.S: kernel_thread_entry)
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    
    // 设置状态寄存器
    // SPP=1 (Supervisor): 因为是内核线程，运行在 S 模式
    // SPIE=1: 启用中断
    // SIE=0: 当前暂时关闭中断
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    
    // 设置入口点为 kernel_thread_entry (entry.S)
    tf.epc = (uintptr_t)kernel_thread_entry;
    
    // 调用 do_fork 创建进程，CLONE_VM 表示共享内存 (内核线程必须共享)
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - 分配大小为 KSTACKPAGE 的页作为进程内核栈
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - 释放进程内核栈的内存空间
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// copy_mm - 根据 clone_flags 复制或共享 "current" 进程的 mm
//         - 如果 clone_flags & CLONE_VM，则 "共享"；否则 "复制"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    assert(current->mm == NULL); // 当前只能是内核线程，mm 必须为空
    /* 在本项目中 (Lab4) 不做任何事，因为内核线程共享内核内存 */
    return 0;
}

// copy_thread - 在进程的内核栈顶设置 trapframe
//             - 并设置进程的内核入口点和栈
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // 1. 在内核栈顶预留空间存放 trapframe
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    
    // 2. 将传入的 tf (模板) 复制到栈顶
    *(proc->tf) = *tf;

    // 3. 设置返回值 a0 为 0，让子进程知道它是刚刚被 fork 出来的
    proc->tf->gpr.a0 = 0;
    // 设置栈指针 (如果是内核线程，esp通常为0，使用刚刚分配的栈顶)
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    // 4. 设置上下文 (Context)，给 switch_to 用
    // ra 设置为 forkret 的地址 -> switch_to 返回后会跳转到 forkret
    proc->context.ra = (uintptr_t)forkret;
    // sp 设置为 trapframe 的地址 -> switch_to 切换后，内核栈指针指向这里
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork - 父进程为新的子进程创建分支
 * @clone_flags: 用于指导如何克隆子进程的标志
 * @stack:       父进程的用户栈指针。如果 stack==0，表示 fork 一个内核线程。
 * @tf:          trapframe 信息，将被复制到子进程的 proc->tf
 */
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    // LAB4:EXERCISE2 YOUR CODE
    /*
     * 一些有用的宏、函数和定义：
     *   alloc_proc:   创建 proc 结构体并初始化字段 (lab4:exercise1)
     *   setup_kstack: 分配大小为 KSTACKPAGE 的页作为进程内核栈
     *   copy_mm:      根据 clone_flags 复制或共享进程 "current" 的 mm
     *                 如果 clone_flags & CLONE_VM，则 "共享"；否则 "复制"
     *   copy_thread:  在进程的内核栈顶设置 trapframe
     *                 并设置进程的内核入口点和栈
     *   hash_proc:    将 proc 加入到进程哈希列表
     *   get_pid:      为进程分配唯一的 pid
     *   wakeup_proc:  设置 proc->state = PROC_RUNNABLE
     * 变量：
     *   proc_list:    进程集合的链表
     *   nr_process:   进程集合的数量
     */

    //    1. 调用 alloc_proc 分配一个 proc_struct
    //    2. 调用 setup_kstack 为子进程分配内核栈
    //    3. 调用 copy_mm 根据 clone_flag 复制或共享 mm (Lab4中为空操作)
    //    4. 调用 copy_thread 在 proc_struct 中设置 tf 和 context
    //    5. 将 proc_struct 插入到 hash_list 和 proc_list
    //    6. 调用 wakeup_proc 使新子进程变为 RUNNABLE (就绪态)
    //    7. 使用子进程的 pid 设置返回值 ret
    
    // 注意：如果上述任何步骤失败，需要跳转到错误处理代码 (如 bad_fork_cleanup_kstack)
    //调用 alloc_proc 来分配一个 proc_struct
    proc=alloc_proc();
    if(proc == NULL)
    {
        goto fork_out; 
    }
    //调用 setup_kstack 来为子进程分配一个内核栈
    int  setup_ret=setup_kstack(proc);
    if(setup_ret!=0)
    {
        goto bad_fork_cleanup_proc;//分配内核栈失败时需要释放proc
    }
    //调用 copy_mm 根据 clone_flag 复制或共享 mm
    int copy_ret=copy_mm(clone_flags,proc);
    if(copy_ret!=0)
    {
        goto bad_fork_cleanup_kstack;//释放proc和kstack
    }
    //调用 copy_thread 在 proc_struct 中设置 tf 和 context
    copy_thread(proc,stack,tf);
    //将 proc_struct 插入 hash_list 和 proc_list
    proc->pid=get_pid();//获取子进程 pid
    hash_proc(proc);//插入hash_list
    list_add(&proc_list,&(proc->list_link));//插入proc_list
    nr_process++;
    //调用 wakeup_proc 使新的子进程进入 RUNNABLE 状态
    wakeup_proc(proc);
    //使用子进程的 pid 设置返回值
    ret=proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc); // 释放内核栈
bad_fork_cleanup_proc:
    kfree(proc);      // 释放 proc 结构体
    goto fork_out;
}

// do_exit - 由 sys_exit 调用
//   1. 调用 exit_mmap & put_pgdir & mm_destroy 释放进程几乎所有的内存空间
//   2. 将进程状态设置为 PROC_ZOMBIE，然后调用 wakeup_proc(parent) 请求父进程回收自己
//   3. 调用调度器切换到其他进程
int do_exit(int error_code)
{
    panic("process exit!!.\n");
}

// init_main - 第二个内核线程，用于创建 user_main 内核线程
static int
init_main(void *arg)
{
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

// proc_init - 设置第一个内核线程 idleproc "idle" (它本身)
//           - 创建第二个内核线程 init_main
void proc_init(void)
{
    int i;

    // 初始化链表
    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    // 1. 创建 idleproc (PID=0)
    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot alloc idleproc.\n");
    }

    // 检查 proc 结构体是否正确初始化 (Ex 1 的检查)
    int *context_mem = (int *)kmalloc(sizeof(struct context));
    memset(context_mem, 0, sizeof(struct context));
    int context_init_flag = memcmp(&(idleproc->context), context_mem, sizeof(struct context));

    int *proc_name_mem = (int *)kmalloc(PROC_NAME_LEN);
    memset(proc_name_mem, 0, PROC_NAME_LEN);
    int proc_name_flag = memcmp(&(idleproc->name), proc_name_mem, PROC_NAME_LEN);

    if (idleproc->pgdir == boot_pgdir_pa && idleproc->tf == NULL && !context_init_flag && idleproc->state == PROC_UNINIT && idleproc->pid == -1 && idleproc->runs == 0 && idleproc->kstack == 0 && idleproc->need_resched == 0 && idleproc->parent == NULL && idleproc->mm == NULL && idleproc->flags == 0 && !proc_name_flag)
    {
        cprintf("alloc_proc() correct!\n");
    }

    // 手动设置 idleproc 的属性
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack; // idleproc 使用启动时的 bootstack
    idleproc->need_resched = 1; // 标记需要调度，以便 cpu_idle 循环一开跑就切换
    set_proc_name(idleproc, "idle");
    nr_process++;

    current = idleproc; // 当前进程设为 idleproc

    // 2. 创建 initproc (PID=1)
    // kernel_thread 会调用 do_fork 来创建新线程
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0)
    {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - 在 kern_init 结束时执行，第一个内核线程 idleproc 将在此循环
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule(); // 如果需要调度，则让出 CPU
        }
    }
}