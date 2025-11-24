#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <defs.h>
#include <intr.h>
#include <riscv.h>

// 保存当前中断状态并禁用中断
// 返回值: 1 表示之前是开中断的，0 表示之前是关中断的
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable(); // 如果之前开启了中断，现在关闭它
        return 1;
    }
    return 0;
}

// 恢复中断状态
// 如果 flag 为 1，说明之前是开中断的，现在重新开启
static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

// 定义宏来简化使用
// 使用 do { ... } while (0) 是为了保证宏展开后的语法安全性
// 用法:
// bool flags;
// local_intr_save(flags);
// ... 临界区代码 ...
// local_intr_restore(flags);

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */