#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <sync.h>

// 前置声明
struct mm_struct;

// 虚拟连续内存区域 (Virtual Memory Area, VMA)
// 描述一段连续的虚拟地址空间 [vm_start, vm_end)
// 属于该 VMA 的地址 addr 满足: vma.vm_start <= addr < vma.vm_end
struct vma_struct {
    struct mm_struct *vm_mm; // 该 VMA 所属的内存管理结构体 (使用同一个页表)
    uintptr_t vm_start;      // VMA 的起始地址
    uintptr_t vm_end;        // VMA 的结束地址 (不包含自身，开区间)
    uint32_t vm_flags;       // VMA 的标志位 (如读/写/执行权限)
    list_entry_t list_link;  // 线性链表节点，按照 vm_start 从小到大排序
};

// 将链表节点转换为 vma_struct 指针
#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

// VMA 权限标志位
#define VM_READ                 0x00000001  // 可读
#define VM_WRITE                0x00000002  // 可写
#define VM_EXEC                 0x00000004  // 可执行

// 内存管理结构体 (mm_struct)
// 用于管理使用同一个页目录表 (PDT) 的一组 VMA
// 通常每个进程拥有一个 mm_struct
struct mm_struct {
    list_entry_t mmap_list;        // VMA 链表的头节点 (按地址排序)
    struct vma_struct *mmap_cache; // 当前访问的 VMA 缓存，用于加速查找 (利用局部性原理)
    pde_t *pgdir;                  // 这些 VMA 所使用的页目录表 (PDT) 的内核虚拟基址
    int map_count;                 // VMA 的数量
    void *sm_priv;                 // 用于交换管理器 (swap manager) 的私有数据 (Lab4暂未使用)
};

// 根据地址 addr 查找所属的 VMA
struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);

// 创建一个新的 VMA
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags);

// 将 VMA 插入到 mm_struct 的链表中
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);

// 创建并初始化一个新的 mm_struct
struct mm_struct *mm_create(void);

// 销毁 mm_struct 及其包含的所有 VMA
void mm_destroy(struct mm_struct *mm);

// 初始化虚拟内存管理 (Lab4 中主要用于自检)
void vmm_init(void);

// 处理缺页异常 (Page Fault) 的核心函数 (Lab4 未实现完整功能，Lab5 重点)
int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);

extern volatile unsigned int pgfault_num; // 缺页异常计数器
extern struct mm_struct *check_mm_struct; // 用于检查的 mm_struct

#endif /* !__KERN_MM_VMM_H__ */