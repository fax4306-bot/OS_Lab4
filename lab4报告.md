# Lab4

# 练习1：分配并初始化一个进程控制块（需要编码）
### 1.设计实现过程
- 设计思路：
alloc_proc 函数的主要职责是为新进程创建一个进程控制块（PCB），并对其所有的成员变量进行默认初始化。由于 kmalloc 分配的内存可能包含残留的垃圾数据，如果不对其进行初始化，可能会导致后续内核调度或执行时出现未定义的错误。
- 具体实现步骤：
  
  - 内存分配：
调用 kmalloc(sizeof(struct proc_struct)) 函数在内核堆中申请一块内存，大小为进程控制块结构体的大小。
  - 检查分配结果：
判断返回的指针 proc 是否为 NULL。如果分配失败，直接返回 NULL。
  - 初始化成员变量：
如果分配成功，对 struct proc_struct 中的各个成员进行初始化，确保它们处于一个“干净”且安全的初始状态：

    (1) 基本信息：
    state 设为 PROC_UNINIT（未初始化状态），表示进程正在创建中，尚未就绪。
    pid 设为 -1，表示尚未分配有效的进程 ID。
    name 使用 memset 清零。

    (2) 资源记录：
    runs（运行时间）、kstack（内核栈地址）、need_resched（调度标志）、flags 均初始化为 0。

    (3) 指针与关联：
    parent（父进程）、mm（内存管理结构）、tf（中断帧）均初始化为 NULL 或 0，pgdir（页表基址）设置为boot_pgdir_pa，默认为共享内核页表，用户进程会在do fork中覆盖。

    (4) 上下文：
    context 结构体包含寄存器信息，使用 memset 将其所有字节清零，防止残留的寄存器值影响后续的上下文切换。
- 代码实现
    ```c
    alloc_proc(void)
    {
        struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
        if (proc != NULL)
        {
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

    ```

### 2.请说明proc_struct中struct context context和struct trapframe *tf成员变量含义和在本实验中的作用是啥？（提示通过看代码和编程调试可以判断出来）
- struct context context (进程上下文)
    - 含义：保存了进程在内核中进行上下文切换（Switch）时所需的寄存器状态。它只包含**被调用者保存 (Callee-saved) 的寄存器（如 s0-s11, ra, sp）。**

       - 注:为什么我们不需要保存所有的寄存器呢？这里我们巧妙地利用了编译器对于函数的处理。我们知道寄存器可以分为调用者保存（caller-saved）寄存器和被调用者保存（callee-saved）寄存器。因为线程切换在一个函数当中，所以编译器会自动帮助我们生成保存和恢复调用者保存寄存器的代码，在实际的进程切换过程中我们只需要保存被调用者保存寄存器就好啦！
    - 作用：
用于 switch_to 函数（汇编）。
当调度器决定暂停当前进程 A 并运行进程 B 时，会把 A 的寄存器保存到 A 的 context 中，并从 B 的 context 中恢复寄存器。
  - 核心：它是线程之间切换的桥梁。

    

- struct trapframe *tf (中断帧)
    - 含义：保存了进程在发生中断、异常或系统调用瞬间的完整现场（包括所有通用寄存器 x0-x31、epc、status 等）。**它总是位于该进程内核栈的顶端。**
    - 作用：
从用户态切换到内核态时：硬件和 trapentry.S 会自动保存现场到 tf，用于处理完中断后恢复用户程序执行。
**在创建内核线程时 (Lab 4 特有)：我们利用 tf 来伪造一个“刚从中断返回”的现场。通过设置 tf->epc = kernel_thread_entry 和 tf->gpr.s0 = fn，使得新线程启动时能跳转到指定的函数执行。**
    - 核心：它是特权级之间切换或线程启动的信使。

# 练习2：为新创建的内核线程分配资源（需要编码）
### 1.设计实现过程
首先，调用 alloc_proc() 分配一个新的 proc_struct，如果分配失败则直接返回错误；接着，通过 setup_kstack(proc) 为子进程分配内核栈，如果失败则释放已分配的 proc_struct。然后，根据 clone_flags 调用 copy_mm() 将父进程的内存空间复制或共享给子进程，如果失败则释放内核栈和 proc_struct。接下来，copy_thread(proc, stack, tf) 设置子进程的陷入帧和内核上下文，使子进程可以从内核入口点开始执行。完成初始化后，通过 get_pid() 分配唯一的子进程 PID，再分别将进程插入哈希链表和全局 proc_list，同时更新系统中进程数量 nr_process。最后调用 wakeup_proc(proc) 将子进程状态设置为可运行，并将子进程的 PID 作为 do_fork 的返回值，确保调用者可以识别新创建的进程。对应的代码实现如下。
```c
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
```
### 2.请说明ucore是否做到给每个新fork的线程一个唯一的id？请说明你的分析和理由。
可以做到，分析和理由如下:

`get_pid()`代码的功能是为系统分配一个唯一的进程标识符 PID。函数内部首先通过静态断言确保支持的最大PID（`MAX_PID`）大于系统能够同时存在的最大进程数（`MAX_PROCESS`），保证 PID 空间足够使用。接着，定义了两个静态变量`last_pid`和`next_safe`，分别记录上一次分配的PID以及下一个安全的PID上限，这两个变量均在第一次调用该函数时初始化为`MAX_PID`。

每次调用函数时，last_pid会先自增一位，如果超过最大 PID，就从1开始重新循环，并进入inside处执行关键的while循环。这个while循环通过遍历`proc_list`链表，逐一比对`last_pid`与已有进程的 PID，如果发现冲突，说明该 PID 已经被占用。算法会将`last_pid`加 1（如果加1后超过`next_safe`或达到`MAX_PID`，就将`last_pid`置1并重置`next_safe`），并跳转回 repeat 标签重新执行while循环开始整个链表的扫描。这确保了只要有冲突，就不会返回当前的`last_pid`。同时在遍历链表时，如果发现某个进程的 PID 大于当前的`last_pid`，代码会记录下这些大于`last_pid`的 PID 中最小的一个，赋值给`next_safe`。这意味着在 last_pid 到 next_safe 之间的这段整数区间内（不包含边界值），没有任何已存在的进程 PID。因此，下一次调用`get_pid`时，只要`++last_pid < next_safe`，就可以直接返回`last_pid`，而无需再次遍历整个链表。这在保证唯一性的同时提高了分配效率。

由此可以分析出，ucore可以做到给每个新fork的线程一个唯一的id。理由在于算法会维护一个静态变量`next_safe`，记录当前已分配 PID 的安全上限。对于候选 PID 小于`next_safe`的情况，算法直接分配，不需要遍历整个进程列表；而候选 PID ≥ next_safe分配 PID 时，算法遍历当前所有进程的列表，一旦发现候选 PID 已存在，立即生成新候选值并重新检查，直到找到一个未被占用的值。并且开头使用了`static_assert(MAX_PID > MAX_PROCESS)`，保证了 PID 的总空间大于系统允许的最大进程数，因此在逻辑上一定能找到一个空闲的 PID，不会出现死循环。
```c
static int get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
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
```
# 练习3：编写proc_run 函数（需要编码）
### 1. 设计实现过程

- **设计思路**：
  `proc_run(struct proc_struct *proc)` 函数是 uCore 中进程切换的核心函数。它的主要职责是将 CPU 的控制权从当前进程（`current`）切换到指定的下一个进程（`proc`）。为了保证切换过程的安全性和正确性，需要完成关闭中断、切换页表、刷新 TLB、切换上下文等一系列操作。同时，为了使调度器能够正常工作，还需要修正底层的页表切换函数（`lsatp`）以及在时钟中断中添加调度标记。

- **具体实现步骤**：

  1.  **检查是否需要切换**：
      首先检查要切换的目标进程 `proc` 是否等于当前进程 `current`。如果相等，说明不需要切换，直接返回。

  2.  **关中断保护临界区**：
      定义 `bool intr_flag`，并调用 `local_intr_save(intr_flag)` 关闭中断。
      **原因**：进程切换涉及修改全局变量 `current` 和硬件寄存器（satp），这是一个临界区操作，如果在切换过程中发生中断（如时钟中断），可能会导致调度器状态混乱，甚至系统崩溃。

  3.  **切换当前进程指针**：
      将 `current` 全局指针指向新的进程 `proc`。

  4.  **切换页表**：
      调用 `lsatp(next->pgdir)` 将新进程的页目录表物理地址加载到 `satp` 寄存器中。
     **底层修正**：由于实验环境是 64 位 RISC-V (SV39 模式)，原有的 `lsatp` 宏定义可能适用于 32 位系统，我们需要将其修改为使用 64 位模式位：
      ```c
      write_csr(satp, (0x8000000000000000ULL) | (pgdir >> RISCV_PGSHIFT));
      ```

  5.  **刷新 TLB**：
      调用 `flush_tlb()`。因为页表基址（satp）发生了变化，TLB中的旧缓存失效，必须刷新以确保 CPU 使用新的地址映射。

  6.  **切换上下文**：
      调用汇编函数 `switch_to(&(prev->context), &(next->context))`。
      **原理**：该函数会将当前 CPU 的寄存器（ra, sp, s0-s11）保存到 `prev` 的上下文中，并将 `next` 上下文中的寄存器值加载到 CPU 中，执行完该指令后，CPU 的执行流实际上已经跳转到了新进程的代码中。

  7.  **恢复中断**：
      调用 `local_intr_restore(intr_flag)`。
      **注意**：这行代码是在 `switch_to` 返回后执行的，这意味着当当前进程在未来某个时刻被重新调度回来时，CPU 会从这里继续执行，并恢复中断状态。

  8.  **调度触发逻辑补充 (Trap)**：
      为了让调度器能够按时间片轮转，我们在 `kern/trap/trap.c` 的 `IRQ_S_TIMER` 时钟中断处理中添加了 `current->need_resched = 1;`。这告诉系统当前进程时间片用尽，需要让出 CPU。

- **代码实现**：

  **proc_run 函数实现 (kern/process/proc.c):**
  ```c
  void proc_run(struct proc_struct *proc) {
      if (proc != current) {
          bool intr_flag;
          struct proc_struct *prev = current, *next = proc;
          // 1. 关中断：保证切换过程原子性，不被时钟中断打断
          local_intr_save(intr_flag);
          {
              // 2. 切换当前进程指针
              current = proc;
              
              // 3. 切换页表，这里用到 lsatp
              // 将新进程的页表基址加载到 satp 寄存器
              lsatp(next->pgdir); 
              
              // 4. 刷新 TLB
              // 页表切换后，必须刷新快表
              flush_tlb();
              
              // 5. 切换上下文
              // 保存原进程寄存器，加载新进程寄存器
              // 执行完这一步，CPU 执行流转移到新进程
              switch_to(&(prev->context), &(next->context));
          }
          // 6. 恢复中断
          // 这行代码是在 switch_to 返回后执行的，也就是当 CPU 再次切换回这个进程时，才会执行这一行。
          local_intr_restore(intr_flag);
      }
  }
  ```

  **相关辅助代码修正 (libs/riscv.h & kern/trap/trap.c):**
  ```c
  // libs/riscv.h: 修正 SV39 模式下的 satp 设置
  static inline void lsatp(unsigned int pgdir) {
      // 0x8000000000000000ULL 代表 SV39 模式 (Mode = 8)
      write_csr(satp, (0x8000000000000000ULL) | (pgdir >> RISCV_PGSHIFT));
  }

  // kern/trap/trap.c: 时钟中断触发调度
  case IRQ_S_TIMER:
      // ... (更新时钟和计数器) ...
      // 触发进程调度：
      current->need_resched = 1; 
      break;
  ```

### 2. 在本实验的执行过程中，创建且运行了几个内核线程？

在本实验的执行过程中，总共创建且运行了 **2** 个内核线程。

**分析与理由：**

1.  **第 0 个内核线程：idleproc (空闲线程)**
    *   **创建**：在 `kern_init` 调用 `proc_init` 时，系统首先通过 `alloc_proc` 创建了 `idleproc`。
    *   **身份**：它是系统启动时的原始执行流。`proc_init` 将其 pid 初始化为 0，并将其状态设置为 `PROC_RUNNABLE`，`kstack` 指向启动时的 `bootstack`。
    *   **运行**：当 `kern_init` 执行到最后调用 `cpu_idle()` 时，`idleproc` 开始正式运行。它的主要工作是在系统空闲（没有其他可运行进程）时进行无限循环，并不断检查 `need_resched` 标志以触发调度。

2.  **第 1 个内核线程：initproc (初始化线程)**
    *   **创建**：在 `proc_init` 函数中，`idleproc` 创建完成后，立即通过 `kernel_thread(init_main, ...)` 创建了 `initproc`。`kernel_thread` 内部调用了 `do_fork`，为其分配了独立的 PCB 和内核栈。
    *   **身份**：`do_fork` 将其 pid 分配为 1，并设置其名称为 "init"。它拥有自己独立的上下文（Context）和中断帧（TrapFrame）。
    *   **运行**：`idleproc` 在 `cpu_idle` 循环中发现 `need_resched` 为 1（初始化时设置），调用 `schedule()`。调度器在进程队列中发现了处于 `PROC_RUNNABLE` 状态的 `initproc`，于是调用 `proc_run` 切换到 `initproc`。`initproc` 执行 `init_main` 函数，打印 "Hello world!!" 等字符串。

**结论**：
系统启动后，首先运行的是 **idleproc**，随后通过调度切换到了 **initproc**。虽然 `initproc` 执行完 `init_main` 后会调用 `do_exit` 并 panic（因为 Lab4 未实现完整的回收机制），但在实验观察到的生命周期内，这两个内核线程确实被创建并运行了。
# 运行结果

### 1. make qemu 执行结果

在终端执行 `make qemu` 后，内核成功启动并按预期执行了各项初始化检查，随后调度运行了第 1 个内核线程 `initproc`，输出如下：

```
OpenSBI v0.4 (Jul  2 2019 11:53:53)
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name          : QEMU Virt Machine
Platform HART Features : RV64ACDFIMSU
Platform Max HARTs     : 8
Current Hart           : 0
Firmware Base          : 0x80000000
Firmware Size          : 112 KB
Runtime SBI Version    : 0.1

PMP0: 0x0000000080000000-0x000000008001ffff (A)
PMP1: 0x0000000000000000-0xffffffffffffffff (A,R,W,X)
DTB Init
HartID: 0
DTB Address: 0x82200000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed
(THU.CST) os is loading ...

Special kernel symbols:
  entry  0xc020004e (virtual)
  etext  0xc0203f54 (virtual)
  edata  0xc0209030 (virtual)
  end    0xc020d4f0 (virtual)
Kernel executable memory footprint: 54KB
memory management: default_pmm_manager
physcial memory map:
  memory: 0x08000000, [0x80000000, 0x87ffffff].
vapaofset is 18446744070488326144
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
use SLOB allocator
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
alloc_proc() correct!
++ setup timer interrupts
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
kernel panic at kern/process/proc.c:437:
    process exit!!.
```

**结果分析：**
1.  **初始化检查通过**：`check_alloc_page`、`check_pgdir`、`check_boot_pgdir`、`alloc_proc` 等输出表明物理内存管理、虚拟内存映射及进程控制块分配功能均正常工作。
2.  **进程调度成功**：输出 `this initproc, pid = 1, name = "init"` 证明 `initproc` 线程被成功创建并调度执行。
3.  **功能验证**：`To U: "Hello world!!"` 等语句的打印，证明 `kernel_thread` 中伪造的中断帧和上下文切换逻辑正确，CPU 成功跳转到了 `init_main` 函数入口。
4.  **预期 Panic**：最后的 `kernel panic` 是因为 Lab4 尚未实现完整的进程回收机制（`do_exit`），当 `initproc` 执行完毕返回时会触发 panic，这符合实验预期，标志着实验流程完整结束。

### 2. make grade 评分结果

执行 `make grade` 进行自动评分，结果如下：

```
  -check alloc proc:                         OK
  -check initproc:                           OK
Total Score: 30/30
```

**结果分析：**
评测脚本对 `alloc_proc` 的初始化逻辑以及 `initproc` 的创建与执行进行了检查，所有测试项均通过，获得满分，证明代码实现符合实验要求。
# 扩展练习 Challenge：
### 1. 说明语句local_intr_save(intr_flag);....local_intr_restore(intr_flag);是如何实现开关中断的？
**1. 代码层面的执行流程**
   - 第一步：关中断并保存状态 (local_intr_save)
  
        宏定义展开后，实际上执行了 __intr_save 函数：
        ```c
        static inline bool __intr_save(void) {
            // 1. read_csr(sstatus): 读取当前的 sstatus 寄存器
            // 2. & SSTATUS_SIE: 检查其中的 SIE (Supervisor Interrupt Enable) 位
            //    SIE = 1 表示当前中断是开启的，SIE = 0 表示当前中断已经关闭
            if (read_csr(sstatus) & SSTATUS_SIE) {
                // 3. 如果原来是开着的，那就把它关掉
                intr_disable(); // 内部调用 clear_csr(sstatus, SSTATUS_SIE)
                return 1;       // 返回 1，表示“也就是关之前，它是开着的”
            }
            // 4. 如果原来就是关着的，那就什么都不用做（保持关闭）
            return 0;           // 返回 0，表示“关之前，它本来就是关着的”
        }
        ```
        执行完这句话，CPU 的中断一定被关闭了（SIE 位被清零）。
        变量 intr_flag 里保存了关闭之前的状态（是开还是关）。
   - 第二步：临界区代码
  
        在 save 和 restore 中间的代码执行时，因为 SIE 位被清零，CPU 会忽略时钟中断和其他外设中断。这保证了这段代码的原子性，不会被调度器打断。
   - 第三步：恢复中断状态 (local_intr_restore)
        宏定义展开后，执行了 __intr_restore 函数：
        ```c
        static inline void __intr_restore(bool flag) {
            // flag 就是刚才保存的那个 intr_flag
            if (flag) {
                // 如果 flag 是 1，说明进临界区之前中断是开着的
                // 那么现在我办完事了，就要负责把它重新打开
                intr_enable(); // 内部调用 set_csr(sstatus, SSTATUS_SIE)
            }
            // 如果 flag 是 0，说明进临界区之前中断本来就是关着的（比如在中断处理函数里）
            // 那么为了保持原样，我什么都不做，继续让它关着。
        }
        ```
**2. 为什么要这么设计？**
```c
// 错误示范
intr_disable(); // 关中断
// ... 干活 ...
intr_enable();  // 开中断
```
这种写法的致命缺陷：不支持嵌套调用

设想这样一个场景：
- 函数 A 正在运行，为了保护数据，它调用了 intr_disable() 关中断。
- 函数 A 调用了 函数 B。
- 函数 B 也觉得自己的代码很重要，于是它也执行了一套“关中断 -> 干活 -> 开中断”的逻辑。
```
函数 A: intr_disable()  --> 此时中断关闭
    |
    +--> 调用 函数 B
         函数 B: intr_disable() --> 中断保持关闭（没问题）
         函数 B: ...干活...
         函数 B: intr_enable()  --> 【中断被打开了！】
         函数 B: 返回
    |
函数 A: ...继续干活...  <-- 此时函数 A 以为中断还是关着的，但实际上已经被 B 打开了！
函数 A: 此时发生了时钟中断，A 被强制切走，数据被破坏。
```
正确做法：保存并恢复 (local_intr_save/restore)
```
函数 A: local_intr_save(flagA) --> flagA = 1, 中断关闭
    |
    +--> 调用 函数 B
         函数 B: local_intr_save(flagB) --> flagB = 0 (因为已经被 A 关了), 中断关闭
         函数 B: ...干活...
         函数 B: local_intr_restore(flagB) --> 因为 flagB 是 0，所以不打开中断！
         函数 B: 返回
    |
函数 A: ...继续干活... <-- 此时中断依然是关闭的，安全！
函数 A: local_intr_restore(flagA) --> 因为 flagA 是 1，此时才真正打开中断。
```
**总结**：这两句语句通过操作 sstatus 寄存器的 SIE 位来实现开关中断，但其精髓在于 “保存原状态” (intr_flag)。
### 2.深入理解不同分页模式的工作原理（思考题）
**get_pte()函数（位于kern/mm/pmm.c）用于在页表中查找或创建页表项，从而实现对指定线性地址对应的物理页的访问和映射操作。这在操作系统中的分页机制下，是实现虚拟内存与物理内存之间映射关系非常重要的内容。**

**(1) get_pte()函数中有两段形式类似的代码， 结合sv32，sv39，sv48的异同，解释这两段代码为什么如此相像。**
- 代码分析：
get_pte() 函数中的这两段代码分别对应了 一级页表（Page Directory 1） 和 二级页表（Page Directory 0） 的查找与分配逻辑。它们的逻辑完全一致：

    - 根据虚拟地址的高位索引（如 PDX1 或 PDX0）找到当前页目录项。
  
    - 检查该表项的 PTE_V（有效位）是否存在。
  
    - 如果不存在且 create 参数为真，则分配一个新的物理页作为下一级页表，清零该页，并建立指向它的映射。
  
    - 获取下一级页表的基址，准备进行下一轮查找。
  
- 结合 SV32 / SV39 / SV48 的异同：
多级页表本质上是一个多叉树结构。不管页表有多少级，处理中间节点的逻辑是通用的（查找 -> 判空 -> 分配 -> 链接）。

    - SV32 (2级页表)：虚拟地址分为 VPN[1]-VPN[0]-Offset。只需要1次中间分配逻辑（查找一级，分配二级）。
  
    - SV39 (3级页表)：本实验采用的模式。虚拟地址分为 VPN[2]-VPN[1]-VPN[0]-Offset。需要2次中间分配逻辑（查找一级分配二级，查找二级分配三级）。这就是为什么代码中出现了两段类似逻辑的原因。
  
    - SV48 (4级页表)：虚拟地址增加了一级索引。如果有 SV48，代码中则需要3段类似的逻辑。
  
**总结**：这两段代码之所以相像，是因为它们在执行多级页表树状结构遍历时的递归/迭代步骤是同构的。每一层级的处理逻辑（查表、判空、建表）都是相同的，区别仅在于索引的位移量不同。

**(2) 目前get_pte()函数将页表项的查找和页表项的分配合并在一个函数里，你认为这种写法好吗？有没有必要把两个功能拆开？**
- 观点： 在 uCore 这种教学场景下，这种写法是可以接受且高效的；但在追求高扩展性的通用操作系统中，拆开会更好。

- 分析如下：
  - 当前写法（合并）的优点：
  
    - 简洁直观：由于 RISC-V SV39 的层级固定为 3 级，展开写避免了循环或递归带来的额外开销，代码逻辑一目了然。
  
    - 减少函数调用：查找和分配通常是紧密关联的（为了写入通常必须先查找），合并在一起可以减少一次函数调用开销。
  
  - 当前写法的缺点：
    -  违反 DRY 原则 (Don't Repeat Yourself)：相同的逻辑复制了两遍，如果将来修改了页表项的初始化逻辑（例如增加某种权限位），需要同时修改两处代码，容易引入 Bug。
  
    - 扩展性差：如果移植到 SV48（4级）或 SV57（5级），需要手动复制粘贴更多代码块。
  
- 改进建议：

    - 循环/递归实现：可以将页表遍历逻辑写成一个循环，根据层级动态计算索引。这样无论硬件是几级页表，代码都只有一份，通过配置参数即可适配 SV32/39/48。
  
    - 职责分离：虽然目前合并在一起比较方便，但在复杂的内存管理中，将 find_pte（纯查找，不分配）和 walk_pgdir（遍历并构建）分离可能更有利于复用查找逻辑（例如在内存去重，我们只想查找而不想分配新页）。

**结论**：对于 Lab 4 而言，当前写法足够简单且能正常工作，没有强烈的必要拆开；但从通用性角度来看，采用循环结构来适配不同层级的页表是更优的设计。

# 知识点

## 1. 本实验中重要的知识点与对应的 OS 原理

**进程控制块**
进程控制块（PCB）是操作系统感知和管理进程的核心数据结构，理论上它记录了进程的标识符、状态、优先级、寄存器上下文及资源清单等关键信息，是系统并发控制的基础。在本次实验中，`proc_struct` 结构体即为 PCB 的具体实现，它承载了内核线程的调度与执行上下文。二者的关系在于理论抽象与工程实践的映射，差异体现为实验中的 PCB 进行了针对性简化，仅保留了 PID、内核栈、CPU 上下文等维护内核线程运行的最小集，去除了文件描述符、用户权限等复杂属性，体现了内核线程共享地址空间的轻量化特征。

**上下文切换**
上下文切换是实现多道程序并发执行的关键机制，其理论本质是在中断或调度发生时，将当前执行流的处理器状态（如 PC、SP 及通用寄存器）保存至内存，并从内存恢复目标执行流的状态。实验通过 `switch.S` 中的汇编代码与 `struct context` 结构体精确复现了这一过程。实验实现与理论高度一致，但通过遵循 RISC-V 调用约定（Calling Convention），代码仅保存被调用者保存（Callee-saved）寄存器而非全部寄存器，在保证正确性的前提下极大地优化了切换开销，验证了软硬件协同在系统设计中的重要性。

**进程状态模型**
进程状态模型用于描述进程生命周期的流转，理论上的经典五状态模型（新建、就绪、运行、阻塞、退出）明确了进程在不同阶段的行为约束。实验中通过 `proc_state` 枚举定义了类似的生命周期状态，通过 `alloc_proc`、`schedule` 等函数驱动状态跃迁。二者的主要差异在于实验代码并未显式定义“运行态”，而是将处于“就绪态”且被 CPU 指针指向的进程视为运行中，这种工程上的简化实现逻辑上完全等效于理论模型，有效支撑了基于时间片轮转的并发调度逻辑。

**内核线程**
内核线程是本次实验的核心执行实体，理论上线程被定义为 CPU 调度的最小单元，而进程是资源拥有的单位。实验中创建的 `idleproc` 和 `initproc` 共享同一套内核页表，没有独立的用户地址空间，这精确对应了理论中关于内核线程“运行在内核态、共享内存资源、拥有独立执行栈”的定义。通过为每个线程分配独立的内核栈和中断帧，实验成功演示了在不隔离内存资源的前提下，如何通过多执行流的并发实现操作系统最基础的调度机制。

**调度机制与分时复用**
分时复用与进程调度是实现并发错觉的基石，理论指出通过高频切换执行流可使多个程序宏观上“同时”运行。实验通过时钟中断处理逻辑强制设置调度标记，并结合 FIFO 调度策略实现 CPU 的抢占式分配，完美印证了时间片轮转算法的理论机制。这种机制通过硬件中断（“时机”）与软件调度器（“策略”）的配合，将抽象的并发理论转化为具体的代码逻辑，体现了操作系统作为硬件资源管理者的核心职能。

## 2. OS 原理中很重要，但在实验中没有对应上的知识点

**用户态与特权级隔离**
用户态与特权级的隔离保护是操作系统安全性的基石。理论上，现代操作系统严格区分用户模式与监管者模式，限制普通程序直接访问硬件或敏感指令。然而在 Lab4 中，所有运行的线程均全程工作在内核态，未涉及特权级切换、用户栈与内核栈的转换以及用户空间的权限检查，这使得实验环境尚未具备保护操作系统免受恶意程序破坏的能力，该部分将在后续引入用户进程后完善。

**挂起状态与交换技术**
挂起状态与交换技术是解决内存资源受限的重要手段。操作系统理论中详细阐述了挂起模型，包括将暂时无法运行或内存不足时的进程“挂起”并交换至外存。Lab4 目前仅实现了基于内存的常驻进程管理，缺乏对挂起状态的定义及内存-磁盘交换机制的支持，所有创建的进程必须完整驻留在物理内存中，无法处理内存超限的复杂场景。

**进程间通信 (IPC)**
进程间通信与同步机制是多进程协作的关键。理论上，并发进程往往需要通过信号量、消息队列或共享内存进行数据交换与协同。本次实验中的两个内核线程仅通过调度器进行被动的 CPU 切换，彼此之间处于完全隔离状态，不存在任何数据交互或同步互斥操作，未体现操作系统在协调并发任务间复杂逻辑关系时的作用。
