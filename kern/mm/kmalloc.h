#ifndef __KERN_MM_KMALLOC_H__
#define __KERN_MM_KMALLOC_H__

#include <defs.h>

#define KMALLOC_MAX_ORDER       10 // kmalloc 支持的最大阶数 (即 2^10 页)

void kmalloc_init(void);

// 分配 n 字节的内核内存
void *kmalloc(size_t n);
// 释放 objp 指向的内核内存
void kfree(void *objp);

#endif /* !__KERN_MM_KMALLOC_H__ */