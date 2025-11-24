#include <vmm.h>
#include <sync.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <riscv.h>
#include <kmalloc.h>

/*
  VMM (虚拟内存管理) 设计包含两部分：mm_struct (mm) 和 vma_struct (vma)
  
  mm 是内存管理器，管理一组具有相同页目录表 (PDT) 的连续虚拟内存区域。
  vma 是一个连续的虚拟内存区域（例如代码段、数据段、堆栈）。
  在 mm 中，有一个用于 vma 的线性链表（在原版设计中可能还有红黑树，但 uCore 简化版只使用了链表）。

---------------
  mm 相关函数:
   全局函数
     struct mm_struct * mm_create(void)
        - 创建并初始化 mm 结构体
     void mm_destroy(struct mm_struct *mm)
        - 销毁 mm 结构体及内部所有 vma
     int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
        - 处理缺页异常（Lab5 重点）
--------------
  vma 相关函数:
   全局函数
     struct vma_struct * vma_create (uintptr_t vm_start, uintptr_t vm_end,...)
        - 创建 VMA
     void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
        - 将 VMA 插入到 mm 的链表中
     struct vma_struct * find_vma(struct mm_struct *mm, uintptr_t addr)
        - 查找包含指定地址的 VMA
   本地函数
     inline void check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
        - 检查 VMA 是否重叠
---------------
   正确性检查函数
     void check_vmm(void);
     void check_vma_struct(void);
     void check_pgfault(void);
*/

// szx func : print_vma and print_mm
// 辅助调试函数：打印 VMA 信息
void print_vma(char *name, struct vma_struct *vma)
{
    cprintf("-- %s print_vma --\n", name);
    cprintf("   mm_struct: %p\n", vma->vm_mm);
    cprintf("   vm_start,vm_end: %x,%x\n", vma->vm_start, vma->vm_end);
    cprintf("   vm_flags: %x\n", vma->vm_flags);
    cprintf("   list_entry_t: %p\n", &vma->list_link);
}

// 辅助调试函数：打印 mm_struct 及其包含的所有 VMA
void print_mm(char *name, struct mm_struct *mm)
{
    cprintf("-- %s print_mm --\n", name);
    cprintf("   mmap_list: %p\n", &mm->mmap_list);
    cprintf("   map_count: %d\n", mm->map_count);
    list_entry_t *list = &mm->mmap_list;
    for (int i = 0; i < mm->map_count; i++)
    {
        list = list_next(list);
        print_vma(name, le2vma(list, list_link));
    }
}

static void check_vmm(void);
static void check_vma_struct(void);
static void check_pgfault(void);

// mm_create - 分配一个 mm_struct 并初始化它
struct mm_struct *
mm_create(void)
{
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));

    if (mm != NULL)
    {
        list_init(&(mm->mmap_list)); // 初始化 VMA 链表头
        mm->mmap_cache = NULL;       // 初始化缓存为空
        mm->pgdir = NULL;            // 页表暂未分配
        mm->map_count = 0;           // VMA 数量为 0
        mm->sm_priv = NULL;          // 交换管理器私有数据为空
    }
    return mm;
}

// vma_create - 分配一个 vma_struct 并初始化它
// 地址范围: [vm_start, vm_end) —— 左闭右开区间
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags)
{
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));

    if (vma != NULL)
    {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
    }
    return vma;
}

// find_vma - 查找包含地址 addr 的 VMA
// 满足条件: vma->vm_start <= addr < vma->vm_end
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr)
{
    struct vma_struct *vma = NULL;
    if (mm != NULL)
    {
        // 1. 尝试从缓存 (mmap_cache) 中查找
        // 根据局部性原理，程序很可能连续访问同一个区域，缓存能显著提高效率
        vma = mm->mmap_cache;
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr))
        {
            // 2. 如果缓存没命中，则遍历链表查找
            bool found = 0;
            list_entry_t *list = &(mm->mmap_list), *le = list;
            while ((le = list_next(le)) != list)
            {
                vma = le2vma(le, list_link);
                // 检查 addr 是否在当前 VMA 范围内
                if (vma->vm_start <= addr && addr < vma->vm_end)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                vma = NULL;
            }
        }
        // 3. 如果找到了，更新缓存，以便下次快速查找
        if (vma != NULL)
        {
            mm->mmap_cache = vma;
        }
    }
    return vma;
}

// check_vma_overlap - 检查 vma1 和 vma2 是否重叠
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
{
    // 确保 VMA 自身的 start < end
    assert(prev->vm_start < prev->vm_end);
    // 确保前一个 VMA 的结束地址 <= 后一个 VMA 的起始地址
    // (因为是左闭右开区间，所以 end == start 是允许的，表示紧邻)
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end);
}

// insert_vma_struct - 将 vma 插入到 mm 的链表中
// 链表是按照 vm_start 从小到大排序的
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
{
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list);
    list_entry_t *le_prev = list, *le_next;

    list_entry_t *le = list;
    // 1. 遍历链表，找到插入位置
    // 目标是找到 le_prev，使得 le_prev 对应的 vma 在新 vma 之前
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *mmap_prev = le2vma(le, list_link);
        if (mmap_prev->vm_start > vma->vm_start)
        {
            break;
        }
        le_prev = le;
    }

    le_next = list_next(le_prev);

    /* 2. 检查是否重叠 */
    // 检查与前一个节点是否重叠
    if (le_prev != list)
    {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    // 检查与后一个节点是否重叠
    if (le_next != list)
    {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    // 3. 执行插入操作
    vma->vm_mm = mm;
    list_add_after(le_prev, &(vma->list_link));

    mm->map_count++;
}

// mm_destroy - 释放 mm 结构体及内部所有的 vma
void mm_destroy(struct mm_struct *mm)
{
    list_entry_t *list = &(mm->mmap_list), *le;
    // 遍历链表，释放每个 VMA
    while ((le = list_next(list)) != list)
    {
        list_del(le);
        kfree(le2vma(le, list_link)); // 释放 vma 结构体内存
    }
    kfree(mm); // 释放 mm 结构体内存
    mm = NULL;
}

// vmm_init - 初始化虚拟内存管理
//          - 目前只是调用 check_vmm 来检查 VMM 的正确性
void vmm_init(void)
{
    check_vmm();
}

// check_vmm - 检查 VMM 的正确性
static void
check_vmm(void)
{
    check_vma_struct();
    // check_pgfault(); // 本实验暂不检查缺页异常处理

    cprintf("check_vmm() succeeded.\n");
}

// 自检函数：测试 VMA 的创建、插入和查找逻辑
static void
check_vma_struct(void)
{
    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = 10, step2 = step1 * 10;

    int i;
    // 插入一系列 VMA，地址递减
    for (i = step1; i >= 1; i--)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    // 插入一系列 VMA，地址递增
    for (i = step1 + 1; i <= step2; i++)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    // 验证链表是否按照地址排序
    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 1; i <= step2; i++)
    {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    // 验证查找功能 (find_vma)
    for (i = 5; i <= 5 * step2; i += 5)
    {
        // 验证能找到存在的 VMA
        struct vma_struct *vma1 = find_vma(mm, i);
        assert(vma1 != NULL);
        struct vma_struct *vma2 = find_vma(mm, i + 1);
        assert(vma2 != NULL);
        
        // 验证找不到不存在的 VMA (间隙)
        struct vma_struct *vma3 = find_vma(mm, i + 2);
        assert(vma3 == NULL);
        struct vma_struct *vma4 = find_vma(mm, i + 3);
        assert(vma4 == NULL);
        struct vma_struct *vma5 = find_vma(mm, i + 4);
        assert(vma5 == NULL);

        assert(vma1->vm_start == i && vma1->vm_end == i + 2);
        assert(vma2->vm_start == i && vma2->vm_end == i + 2);
    }

    // 验证查找范围外的地址
    for (i = 4; i >= 0; i--)
    {
        struct vma_struct *vma_below_5 = find_vma(mm, i);
        if (vma_below_5 != NULL)
        {
            cprintf("vma_below_5: i %x, start %x, end %x\n", i, vma_below_5->vm_start, vma_below_5->vm_end);
        }
        assert(vma_below_5 == NULL);
    }

    mm_destroy(mm);

    cprintf("check_vma_struct() succeeded!\n");
}