#ifndef __KERN_MM_PMM_H__
#define __KERN_MM_PMM_H__

#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <atomic.h>
#include <assert.h>

/* do_fork 中使用的 fork 标志位 */
#define CLONE_VM 0x00000100     // 若设置，表示在进程间共享虚拟内存（用于线程创建）
#define CLONE_THREAD 0x00000200 // 线程组标志

// 物理内存管理类结构体。
// 一个特殊的 pmm 管理器（例如 default_pmm_manager）只需要实现这个类中的方法，
// ucore 就可以利用它来管理整个物理内存空间。
struct pmm_manager
{
    const char *name;                                 // 物理内存管理器的名称
    void (*init)(void);                               // 初始化内部描述和管理数据结构（如空闲块链表、空闲块数量）
    void (*init_memmap)(struct Page *base, size_t n); // 根据初始的空闲物理内存空间，设置管理数据结构
    struct Page *(*alloc_pages)(size_t n);            // 分配 >=n 个物理页，具体依赖于分配算法
    void (*free_pages)(struct Page *base, size_t n);  // 释放从 base 开始的 >=n 个物理页（根据 memlayout.h 中的 Page 描述符）
    size_t (*nr_free_pages)(void);                    // 返回当前空闲页的数量
    void (*check)(void);                              // 检查物理内存管理器的正确性
};

extern const struct pmm_manager *pmm_manager; // 当前使用的物理内存管理器
extern pde_t *boot_pgdir_va;                  // 启动时页目录表的虚拟地址
extern const size_t nbase;                    // 物理内存基地址对应的页号
extern uintptr_t boot_pgdir_pa;               // 启动时页目录表的物理地址

void pmm_init(void);

struct Page *alloc_pages(size_t n);           // 分配 n 个页
void free_pages(struct Page *base, size_t n); // 释放 n 个页
size_t nr_free_pages(void);                   // 获取空闲页数量

#define alloc_page() alloc_pages(1)           // 分配 1 页的宏
#define free_page(page) free_pages(page, 1)   // 释放 1 页的宏

// 获取虚拟地址对应的页表项，如果不存在且 create 为 true 则创建
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
// 获取虚拟地址对应的 Page 结构体指针
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store);
// 移除虚拟地址的映射
void page_remove(pde_t *pgdir, uintptr_t la);
// 建立物理页与虚拟地址的映射关系
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);

// 刷新指定虚拟地址的 TLB 条目
void tlb_invalidate(pde_t *pgdir, uintptr_t la);
// 为页目录分配一个页（通常用于创建新页表）
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm);

void print_pgdir(void); // 打印页表信息（调试用）

/* *
 * PADDR - 输入一个内核虚拟地址（指向 KERNBASE 之上的地址），
 * 该地址处于机器最大物理内存的直接映射区域，返回对应的物理地址。
 * 如果传入非内核虚拟地址，则会 panic。
 * */
#define PADDR(kva)                                                 \
    ({                                                             \
        uintptr_t __m_kva = (uintptr_t)(kva);                      \
        if (__m_kva < KERNBASE)                                    \
        {                                                          \
            panic("PADDR called with invalid kva %08lx", __m_kva); \
        }                                                          \
        __m_kva - va_pa_offset;                                    \
    })

/* *
 * KADDR - 输入一个物理地址，返回对应的内核虚拟地址。
 * 如果传入无效的物理地址，则会 panic。
 * */
#define KADDR(pa)                                                \
    ({                                                           \
        uintptr_t __m_pa = (pa);                                 \
        size_t __m_ppn = PPN(__m_pa);                            \
        if (__m_ppn >= npage)                                    \
        {                                                        \
            panic("KADDR called with invalid pa %08lx", __m_pa); \
        }                                                        \
        (void *)(__m_pa + va_pa_offset);                         \
    })

extern struct Page *pages;     // 物理页描述符数组
extern size_t npage;           // 物理页总数
extern uint_t va_pa_offset;    // 虚拟地址与物理地址的偏移量

// 根据 Page 结构体获取物理页号 (PPN)
static inline ppn_t
page2ppn(struct Page *page)
{
    return page - pages + nbase;
}

// 根据 Page 结构体获取物理地址 (PA)
static inline uintptr_t
page2pa(struct Page *page)
{
    return page2ppn(page) << PGSHIFT;
}

// 根据物理地址 (PA) 获取 Page 结构体
static inline struct Page *
pa2page(uintptr_t pa)
{
    if (PPN(pa) >= npage)
    {
        panic("pa2page called with invalid pa");
    }
    return &pages[PPN(pa) - nbase];
}

// 根据 Page 结构体获取内核虚拟地址 (KVA)
static inline void *
page2kva(struct Page *page)
{
    return KADDR(page2pa(page));
}

// 根据内核虚拟地址 (KVA) 获取 Page 结构体
static inline struct Page *
kva2page(void *kva)
{
    return pa2page(PADDR(kva));
}

// 根据页表项 (PTE) 获取对应的 Page 结构体
static inline struct Page *
pte2page(pte_t pte)
{
    if (!(pte & PTE_V))
    {
        panic("pte2page called with invalid pte");
    }
    return pa2page(PTE_ADDR(pte));
}

// 根据页目录项 (PDE) 获取对应的 Page 结构体
static inline struct Page *
pde2page(pde_t pde)
{
    return pa2page(PDE_ADDR(pde));
}

// 获取页面的引用计数
static inline int
page_ref(struct Page *page)
{
    return page->ref;
}

// 设置页面的引用计数
static inline void
set_page_ref(struct Page *page, int val)
{
    page->ref = val;
}

// 增加页面的引用计数
static inline int
page_ref_inc(struct Page *page)
{
    page->ref += 1;
    return page->ref;
}

// 减少页面的引用计数
static inline int
page_ref_dec(struct Page *page)
{
    page->ref -= 1;
    return page->ref;
}

// 刷新 TLB
static inline void flush_tlb()
{
    asm volatile("sfence.vma");
}

// 根据物理页号和权限位构造一个页表项 (PTE)
static inline pte_t pte_create(uintptr_t ppn, int type)
{
    return (ppn << PTE_PPN_SHIFT) | PTE_V | type;
}

// 构造一个页目录项 (PDE)
static inline pte_t ptd_create(uintptr_t ppn)
{
    return pte_create(ppn, PTE_V);
}

extern char bootstack[], bootstacktop[]; // 定义在 entry.S 中的内核栈

#endif /* !__KERN_MM_PMM_H__ */