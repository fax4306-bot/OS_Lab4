#ifndef __KERN_MM_MEMLAYOUT_H__
#define __KERN_MM_MEMLAYOUT_H__

/* This file contains the definitions for memory management in our OS. */
/* 此文件包含操作系统中内存管理的定义。 */

/* *
 * Virtual memory map:                                          Permissions
 * 虚拟内存映射图：                                               权限
 *                                                              kernel/user(内核/用户)
 *
 *     4G ------------------> +---------------------------------+
 *                            |                                 |
 *                            |         Empty Memory (*)        | 空闲内存区域
 *                            |                                 |
 *                            +---------------------------------+ 0xFB000000
 *                            |   Cur. Page Table (Kern, RW)    | RW/-- PTSIZE
 *     VPT -----------------> +---------------------------------+ 0xFAC00000
 *                            |        Invalid Memory (*)       | --/--
 *                            |        (无效/未映射内存)         |
 *     KERNTOP -------------> +---------------------------------+ 0xF8000000
 *                            |                                 |
 *                            |    Remapped Physical Memory     | RW/-- KMEMSIZE
 *                            |      (重映射的物理内存区域)       |
 *                            |    (即物理内存直接线性映射区)     |
 *     KERNBASE ------------> +---------------------------------+ 0xC0000000
 *                            |                                 |
 *                            |                                 |
 *                            |                                 |
 *                            ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.
 * (*) 注意：内核确保 "Invalid Memory" 区域永远不会被映射。
 *     "Empty Memory" 通常未被映射，但用户程序可以根据需要在此处映射页面。
 *
 * */

/* All physical memory mapped at this address */
/* 所有物理内存都映射到这个虚拟地址起始处 (RISC-V 64位 Sv39模式下的高地址映射) */
#define KERNBASE            0xFFFFFFFFC0200000
#define KMEMSIZE            0x7E00000                  // the maximum amount of physical memory (物理内存的最大数量，约126MB)
#define KERNTOP             (KERNBASE + KMEMSIZE)      // 内核直接映射区域的结束地址

/* 
 * 物理内存偏移量：虚拟地址 - 物理地址 = 偏移量
 * 例如：虚拟地址 0xFFFFFFFFC0200000 对应 物理地址 0x80200000
 * 0xFFFFFFFFC0200000 - 0x80200000 = 0xFFFFFFFF40000000
 * 用于 PADDR 和 KADDR 宏进行地址转换
 */
#define PHYSICAL_MEMORY_OFFSET      0xFFFFFFFF40000000


#define KSTACKPAGE          2                           // # of pages in kernel stack (内核栈占用的页数)
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // sizeof kernel stack (内核栈的总字节大小 = 2 * 4096 = 8KB)

#ifndef __ASSEMBLER__

#include <defs.h>
#include <atomic.h>
#include <list.h>

typedef uintptr_t pte_t;        // 页表项 (Page Table Entry)
typedef uintptr_t pde_t;        // 页目录项 (Page Directory Entry)
typedef pte_t swap_entry_t;     // the pte can also be a swap entry (页表项也可以作为交换条目)


/*
 * struct Page - 物理页描述符结构体。
 * 每个 Page 结构体管理并描述一个物理页（4KB）。
 * 在 kern/mm/pmm.h 中，你可以找到许多有用的函数，用于将 Page 转换为其他数据类型（如物理地址）。
 * 注意：Page 结构体本身位于内核内存中，它描述的是物理内存的使用情况。
 */
struct Page {
    int ref;                        // 页帧的引用计数。如果为0，表示该页空闲；如果>0，表示被页表映射了多少次（共享内存）。
    uint_t flags;                   // 描述页帧状态的标志位数组（见下文 PG_reserved 等）。
    unsigned int property;          // 空闲块的数量，用于首次适应（First-Fit）物理内存管理器。
                                    // 如果该页是空闲块的头页，则 property 记录连续空闲页的数量。
    list_entry_t page_link;         // 空闲链表节点。用于将空闲页链接到 free_area 的链表中。
    list_entry_t pra_page_link;     // 用于页面置换算法（PRA）的链表节点（Lab 5 使用）。
    uintptr_t pra_vaddr;            // 用于页面置换算法，记录该物理页对应的虚拟地址（Lab 5 使用）。
};


/* 描述页帧状态的标志位定义 */

/* 
 * PG_reserved: 保留位
 * 如果 bit=1：表示该页被内核保留（例如内核代码段、数据段占用的页），不能在 alloc/free_pages 中使用。
 * 否则 bit=0。
 */
#define PG_reserved                 0       

/* 
 * PG_property: 属性位（用于物理内存分配器）
 * 如果 bit=1：表示该页是一个空闲内存块的头页（包含若干连续物理页），可以被 alloc_pages 分配。
 * 如果 bit=0：
 *    情况A：该页虽然是空闲块头页，但已经被分配出去了。
 *    情况B：该页根本不是头页（可能是空闲块中间的某页，或者是已分配页）。
 */
#define PG_property                 1       

// 以下宏用于设置、清除和测试 Page 结构体中的 flags 标志位
// 使用了 atomic.h 中的原子操作，保证并发安全
#define SetPageReserved(page)       set_bit(PG_reserved, &((page)->flags))   // 设置为保留页
#define ClearPageReserved(page)     clear_bit(PG_reserved, &((page)->flags)) // 取消保留状态
#define PageReserved(page)          test_bit(PG_reserved, &((page)->flags))  // 检查是否为保留页

#define SetPageProperty(page)       set_bit(PG_property, &((page)->flags))   // 设置 Property 标志
#define ClearPageProperty(page)     clear_bit(PG_property, &((page)->flags)) // 清除 Property 标志
#define PageProperty(page)          test_bit(PG_property, &((page)->flags))  // 检查 Property 标志

// convert list entry to page
// 将链表节点 list_entry_t 转换为包含它的 struct Page 指针
// 使用了 container_of 机制
#define le2page(le, member)                 \
    to_struct((le), struct Page, member)

/* free_area_t - maintains a doubly linked list to record free (unused) pages */
/* free_area_t - 维护一个双向链表来记录空闲（未被使用）的页 */
typedef struct {
    list_entry_t free_list;         // the list header (空闲页链表的头节点)
    unsigned int nr_free;           // of free pages in this free list (链表中空闲页的总数)
} free_area_t;

#endif /* !__ASSEMBLER__ */

#endif /* !__KERN_MM_MEMLAYOUT_H__ */