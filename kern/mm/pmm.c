#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>
#include <dtb.h>

// 物理页结构数组的虚拟起始地址 (指向所有 struct Page 的数组)
struct Page *pages;
// 物理内存的总量 (以页为单位)
size_t npage = 0;
// 内核镜像映射偏移量：VA = PA + va_pa_offset
// 内核被映射在 VA=KERNBASE 处，物理地址在 info.base (0x80200000)
uint_t va_pa_offset;
// 物理内存起始地址对应的页号 (RISC-V 中内存从 0x80000000 开始)
const size_t nbase = DRAM_BASE / PGSIZE;

// 启动时页目录表的虚拟地址 (boot_pgdir)
pde_t *boot_pgdir_va = NULL;
// 启动时页目录表的物理地址
uintptr_t boot_pgdir_pa;

// 物理内存管理器接口实例 (例如 default_pmm_manager)
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - 初始化 pmm_manager 实例
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - 调用 pmm->init_memmap 来初始化空闲内存块
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - 调用 pmm->alloc_pages 分配 n 个连续的物理页
// 返回分配的第一个页的 Page 结构体指针
struct Page *alloc_pages(size_t n)
{
    struct Page *page = NULL;
    bool intr_flag;
    // 关中断保护，防止分配过程中被中断打断导致链表数据不一致
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag); // 恢复中断状态
    return page;
}

// free_pages - 调用 pmm->free_pages 释放 n 个连续的物理页
void free_pages(struct Page *base, size_t n)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - 调用 pmm->nr_free_pages 获取当前空闲页数量
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

/* pmm_init - 初始化物理内存管理 */
static void page_init(void)
{
    extern char kern_entry[];

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // 从设备树 (DTB) 获取物理内存的起始地址和大小
    uint64_t mem_begin = get_memory_base();
    uint64_t mem_size  = get_memory_size();
    if (mem_size == 0) {
        panic("DTB memory info not available");
    }
    uint64_t mem_end   = mem_begin + mem_size;

    cprintf("physcial memory map:\n");
    cprintf("  memory: 0x%08lx, [0x%08lx, 0x%08lx].\n", mem_size, mem_begin,
            mem_end - 1);

    uint64_t maxpa = mem_end;

    // 限制最大物理地址不超过 KERNTOP (内核直接映射区的上限)
    if (maxpa > KERNTOP)
    {
        maxpa = KERNTOP;
    }

    extern char end[]; // 链接脚本中定义的内核结束地址

    npage = maxpa / PGSIZE;
    
    // pages 数组存放在内核结束后的第一个可用页开始的位置
    // 使用 ROUNDUP 对齐到页边界
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);

    // 初始化所有 Page 结构体为 Reserved (保留)
    for (size_t i = 0; i < npage - nbase; i++)
    {
        SetPageReserved(pages + i);
    }

    // 计算空闲内存的起始地址 (紧接在 pages 数组之后)
    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));

    mem_begin = ROUNDUP(freemem, PGSIZE);
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    
    // 将剩余的空闲内存初始化为可分配状态
    if (freemem < mem_end)
    {
        init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE);
    }
    cprintf("vapaofset is %llu\n", va_pa_offset);
}

// 启用分页机制：将页表基地址写入 satp 寄存器
static void enable_paging(void)
{
    // 0x8000...0000 设置 MODE 字段为 Sv39 (8 << 60)
    write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
}

// boot_map_segment - 建立一段内存的映射关系 (仅用于启动阶段)
// 参数：
//  pgdir: 页目录表基址
//  la:    线性地址 (虚拟地址)
//  size:  映射大小
//  pa:    物理地址
//  perm:  权限标志
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    assert(PGOFF(la) == PGOFF(pa)); // 确保页内偏移一致
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        // 获取或创建页表项
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        // 填入物理页号和权限
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - 分配一页内存用于存放页表
// 返回值：该页的内核虚拟地址
static void *boot_alloc_page(void)
{
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - 初始化物理内存管理，建立页表并开启分页机制
//          - 检查正确性，打印页表信息
void pmm_init(void)
{
    // 1. 初始化物理内存管理器 (First Fit / Best Fit 等)
    init_pmm_manager();

    // 2. 探测物理内存空间，初始化 pages 数组和空闲链表
    page_init();

    // 3. 检查分配/释放函数的正确性
    check_alloc_page();
    
    // 4. 设置启动时的页目录表 (boot_pgdir)
    // boot_page_table_sv39 定义在 entry.S 中
    extern char boot_page_table_sv39[];
    boot_pgdir_va = (pte_t *)boot_page_table_sv39;
    boot_pgdir_pa = PADDR(boot_pgdir_va);

    // 5. 检查页表操作函数的正确性
    check_pgdir();

    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // 6. 检查启动页表的正确性
    check_boot_pgdir();

    // 7. 初始化内核小内存分配器 (Slab/Slob)
    kmalloc_init();
}

// get_pte - 获取虚拟地址 la 对应的页表项 (PTE) 的内核虚拟地址
//        - 如果中间的页表不存在，根据 create 参数决定是否创建
// 参数:
//  pgdir:  页目录表的内核虚拟基地址 (一级页表)
//  la:     需要映射的线性地址 (虚拟地址)
//  create: 如果为 true，当页表不存在时会分配新页创建
// 返回值:  对应 PTE 的内核虚拟地址指针，失败返回 NULL
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    // 1. 查找一级页目录项 (PDX1)
    pde_t *pdep1 = &pgdir[PDX1(la)];
    
    // 如果一级页表项无效 (PTE_V 为 0)
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        // 如果不需要创建，直接返回 NULL
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1); // 设置引用计数
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE); // 清空新分配的页表页 (重要!)
        // 建立一级到二级的链接 (PTE_U | PTE_V)
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    
    // 2. 查找二级页目录项 (PDX0)
    // PDE_ADDR(*pdep1) 获取下一级页表的物理地址，KADDR 转为虚拟地址
    pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    
    // 如果二级页表项无效
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE); // 清空
        // 建立二级到三级的链接
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    
    // 3. 返回三级页表项 (PTX) 的地址
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - 根据页目录表 pgdir 获取线性地址 la 对应的物理页结构
// 参数 ptep_store: 如果不为 NULL，用于传出该 PTE 的地址
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    // 查找 PTE，create=0 表示只查找不创建
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;
    }
    // 如果 PTE 存在且有效，返回对应的 Page 结构体
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

// page_remove_pte - 释放与线性地址 la 关联的物理页，并清除对应的 PTE
//                - 既然页表被修改了，需要刷新 TLB
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if (*ptep & PTE_V)
    { //(1) 检查页表项是否有效
        struct Page *page =
            pte2page(*ptep); //(2) 获取该 PTE 对应的 Page 结构体
        page_ref_dec(page);  //(3) 减少页面的引用计数
        if (page_ref(page) ==
            0)
        { //(4) 如果引用计数变为 0，释放该物理页
            free_page(page);
        }
        *ptep = 0;                 //(5) 清除页表项 (设为 0)
        tlb_invalidate(pgdir, la); //(6) 刷新 TLB，因为映射已改变
    }
}

// page_remove - 移除线性地址 la 的映射，如果映射存在的话
void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0); // 查找 PTE
    if (ptep != NULL)
    {
        page_remove_pte(pgdir, la, ptep); // 执行移除操作
    }
}

// page_insert - 建立物理页 page 与线性地址 la 的映射关系
// 参数:
//  pgdir: 页目录表基址
//  page:  要映射的物理页
//  la:    线性地址 (虚拟地址)
//  perm:  权限标志 (R/W/X/U)
// 返回值: 成功返回 0
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    // 1. 获取 PTE，如果页表不存在则创建
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL)
    {
        return -E_NO_MEM;
    }
    
    // 2. 增加物理页的引用计数 (因为多了一个虚拟地址指向它)
    page_ref_inc(page);
    
    // 3. 检查该地址是否已经存在映射
    if (*ptep & PTE_V)
    {
        struct Page *p = pte2page(*ptep);
        if (p == page)
        {
            // 如果原本就映射到了同一个物理页，抵消掉刚才增加的引用计数
            // (相当于重新设置权限)
            page_ref_dec(page);
        }
        else
        {
            // 如果映射到了其他物理页，先移除旧映射
            page_remove_pte(pgdir, la, ptep);
        }
    }
    
    // 4. 写入新的页表项 (物理页号 | 有效位 | 权限)
    *ptep = pte_create(page2ppn(page), PTE_V | perm);
    
    // 5. 刷新 TLB
    tlb_invalidate(pgdir, la);
    return 0;
}

// tlb_invalidate - 刷新指定虚拟地址的 TLB 条目
// 只有当正在修改的页表就是当前 CPU 使用的页表时才需要刷新
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    // flush_tlb() 会刷新整个 TLB，这里使用 sfence.vma 指令刷新特定地址
    // 这是一个针对 RISC-V 的优化
    asm volatile("sfence.vma %0" : : "r"(la));
}

// 检查 alloc_pages 功能是否正常
static void check_alloc_page(void)
{
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

// 检查页表操作 (get_pte, page_insert, page_remove) 是否正确
static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir_va[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

// 检查启动时的页表映射是否正确
static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// perm2str - 将权限标志转换为字符串 'u,r,w,-' (调试用)
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

// get_pgtable_items - 在页表或页目录的 [left, right] 范围内查找连续的映射条目
// 参数:
//  left:        未使用的参数 ???
//  right:       表范围的上限索引
//  start:       表范围的下限索引
//  table:       表起始地址
//  left_store:  返回下一段连续映射的起始索引
//  right_store: 返回下一段连续映射的结束索引
//  return value: 0 - 未找到有效项, perm - 有效项的权限
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;
    }
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    if (start < right)
    {
        if (left_store != NULL)
        {
            *left_store = start;
        }
        int perm = (table[start++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        if (right_store != NULL)
        {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}