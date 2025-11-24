.text
.globl kernel_thread_entry
kernel_thread_entry:        # void kernel_thread(void)
    # 这是一个内核线程的入口点。
    # 当一个新创建的内核线程首次被调度执行时，它会从这里开始运行。
    # 在 copy_thread 函数中，我们将 tf->epc 设置为了 kernel_thread_entry 的地址。

    # 1. 准备参数
    # 在 kernel_thread 函数中，我们将参数 arg 保存在了 s1 寄存器中。
    # 按照 RISC-V 调用约定，函数的第一个参数需要放在 a0 寄存器中。
    move a0, s1       # a0 = s1 (arg)

    # 2. 调用线程函数
    # 在 kernel_thread 函数中，我们将函数指针 fn 保存在了 s0 寄存器中。
    # 使用 jalr (Jump And Link Register) 指令调用该函数。
    jalr s0           # call fn(arg)

    # 3. 线程退出
    # 如果线程函数 fn 执行完毕返回（return），代码流程会走到这里。
    # 我们调用 do_exit 来清理资源并终止当前线程。
    jal do_exit       # do_exit(error_code)