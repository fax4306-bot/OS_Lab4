#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_pmm.h>
#include <stdio.h> // 为了使用 cprintf
static struct Page *buddy_base; // 用于记录伙伴系统管理的内存块的起始页

// --------------------第一步：核心数据结构设计------------------------------------
// 1.总页数=128MB/4KB=32768=2^15,我们定义一个宏，通常会多留一点余地
#define MAX_ORDER 16
// 2.用于管理所有空闲块的数组，数组下标就是阶数
static free_area_t free_areas[MAX_ORDER];
// 3.总空闲页数
static size_t nr_free;
// 4.此外，我们用struct Page来存储伙伴系统特有的信息
//   page->property: 对于一个空闲块的头节点，我们用这个字段来存储它的阶数 (order)。
//   page->flags 的 PG_property 位: 我们继续用这个位来标记一个页是否为空闲块的头节点。
//   page->page_link: 用于将空闲块链接到 free_areas 数组的某个链表中。

// ---------第二步：实现初始化 (buddy_init & buddy_init_memmap)-------------------
static void
buddy_init(void) 
{
    //初始化free_areas数组
    for(int i=0;i<MAX_ORDER;i++)
    {
        list_init(&free_areas[i].free_list);
        free_areas[i].nr_free=0;
    }
    nr_free=0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) 
{
    // 这里提供一下整个伙伴管理系统的起始地址
    buddy_base =base;
    // 目标: 初始化伙伴系统管理的内存区域
    // 输入: base (起始页指针), n (总页数)
    // (前置准备)
    assert(n > 0);
    struct Page *p=base;
    for(;p!=base+n;p++)
    {
        assert(PageReserved(p));
        p->flags = p->property = 0; 
        set_page_ref(p, 0);
    }

    // (核心循环) 使用一个偏移量 current_offset，从 0 开始，直到处理完所有 n 个页。（贪心）
    size_t current_offset=0;
    while(current_offset<n)
    {
        size_t remainning_pages = n - current_offset;
        // 找到当前剩余页的最大阶数
        size_t order=0;// 阶数
        size_t block_size =1;// 该空闲块的页数
        while((block_size<<1)<=remainning_pages)
        {
            order++;
            block_size<<=1;
        }
        // 找到刚找出的这个块的头节点
        struct Page *head =base+current_offset;
        head->property=order;
        SetPageProperty(head);
        //链入空闲表
        list_add(&free_areas[order].free_list,&head->page_link);
        free_areas[order].nr_free++;
        nr_free+=block_size;
        current_offset+=block_size;
    }
}

// ---------第三步：实现分配 (buddy_alloc_pages)----------------------------------
static struct Page *
buddy_alloc_pages(size_t n) 
{
    // 给定需要的页数n，对他进行分配。返回一个struct Page的指针
    // 安全检查
    if(n==0 || n>nr_free)
        return NULL;
    // 计算最合适的阶数
    size_t order=0;
    size_t block_size=1;
    while(block_size<n)
    {
        order++;
        block_size<<=1;
    }
    size_t best_order=order;
    size_t find_order = best_order; 
    // 从order开始向上找，找到一个合适的空闲块。
    struct Page*block_choose=NULL;
    while(find_order<MAX_ORDER)
    {
        if(!list_empty(&free_areas[find_order].free_list))
        {
            list_entry_t *le=list_next(&free_areas[find_order].free_list);
            // le2page 这个宏需要两个参数：链表项指针 le，和它在 Page 结构体中的成员名 page_link。
            block_choose = le2page(le, page_link);
            list_del(le);
            free_areas[find_order].nr_free--;
            break;
        }
        find_order++;
    }
    if(find_order==MAX_ORDER)
        return NULL;
    // 接着判断这个空闲块是否为最合适的,这是个递归的过程
    while(find_order>best_order)
    {
        //分裂成两半
        size_t current_order=find_order-1;
        block_choose->property=current_order;
        struct Page *friend_block=block_choose+(1<<current_order);
        //把伙伴放回去
        friend_block->property = current_order;
        SetPageProperty(friend_block);
        list_add(&free_areas[current_order].free_list, &friend_block->page_link);
        free_areas[current_order].nr_free++;
        find_order=current_order;
    }
    // 对最终的这个块进行处理
    ClearPageProperty(block_choose);
    nr_free-=block_size;
    return block_choose;
}



// ---------第四步：实现释放 (buddy_free_pages)----------------------------------
static struct Page *get_buddy(struct Page *page, size_t order) 
{
    // 根据伙伴两人之间地址仅相差一位的特征，且这一位就是size最高位
    // 利用异或（A xor B,当B为1时，A的值会被翻转）直接计算出伙伴的地址，注意这里计算的是偏移量
    // 先计算出偏移量（起始地址就是buddy_base）
    size_t block_size = 1<<order;
    size_t offset = page - buddy_base;
    size_t offset_friend = offset ^ block_size;
    cprintf("调试：页索引 %d，阶数 %d，对应伙伴页索引 %d\n", offset, order, offset_friend);
    struct Page *page_friend = buddy_base + offset_friend;
    return page_friend;
}
static void
buddy_free_pages(struct Page *base, size_t n) 
{
    // 给定一个块和他的页数(注意这个n是用户申请的大小而不是我们分配给他的块的页数)，把他重新放入空闲链表中。
    // 重新放入时要考虑是否可以和伙伴进行合并，这也是一个递归过程，如果可以合并就一直继续直至合并到最大阶数
    // 先根据n算出他的阶数
    size_t order=0;
    size_t block_size=1;
    while(block_size<n)
    {
        order++;
        block_size<<=1;
    }
    nr_free+=block_size;
    // 初始准备:将 base 块初始化为一个合法的空闲块
    struct Page *current_block = base;
    current_block->property = order;
    SetPageProperty(current_block);

    while(order < MAX_ORDER-1)
    {
        struct Page *friend_page =get_buddy(current_block,order);
        //cprintf("找到伙伴！！！！！！！！！！！！！！！！！！");
        if(!(PageProperty(friend_page)))
        {
            cprintf("伙伴不满足关系1\n");
            break;
        }
        if(friend_page->property!=order)
        {
            cprintf("伙伴不满足关系2\n");
            break;
        }
        // a. 先将伙伴从它的旧链表中移除 
        list_del(&friend_page->page_link);
        free_areas[order].nr_free--;
        ClearPageProperty(friend_page);

        // b. 确定新块的头 (永远是地址较小的那个)
        if(friend_page<current_block)
            current_block=friend_page;
        // c. 提升阶数，为下一轮循环做准备
        SetPageProperty(current_block);
        order++;  
        current_block->property = order; 
    }
    list_add(&free_areas[order].free_list,&current_block->page_link);
    free_areas[order].nr_free++;
}




static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}


/*
 * LAB2: 以下代码用于检查“首次适应分配算法”（你的练习 EXERCISE 1）
 * 注意：你不应该修改 basic_check 和 buddy_check 函数！
 * (below code is used to check the first fit allocation algorithm.
 *  NOTICE: you SHOULD NOT CHANGE basic_check, buddy_check functions!)
 */
// 辅助函数：打印当前所有空闲链表的状态
static void print_buddy_info(void) {
    cprintf("===== Buddy System 空闲块状态 =====\n");
    for (int i = 0; i < MAX_ORDER; i++) {
        if (!list_empty(&free_areas[i].free_list)) {
            cprintf("  阶数 %d (%d 页): %d 个空闲块\n", i, 1 << i, free_areas[i].nr_free);
        }
    }
    cprintf("====================================\n");
}

static void
buddy_check(void) {
    cprintf("\n--- Buddy System Check 开始 ---\n");

    // 记录初始空闲页总数
    size_t total_free_start = nr_free_pages();
    cprintf("初始空闲页总数: %d\n", total_free_start);
    print_buddy_info();

    // -------------------------------------------------
    // 测试 1: 简单分配与释放
    // -------------------------------------------------
    cprintf("\n[测试 1: 简单分配与释放]\n");
    struct Page *p1, *p2;
    p1 = alloc_pages(5);
    print_buddy_info();
    assert(p1 != NULL);
    cprintf("成功分配 5 页 (应获得一个 8 页的块), 物理地址: 0x%lx\n", page2pa(p1));
    assert(nr_free_pages() == total_free_start - 8);

    p2 = alloc_pages(10);
    print_buddy_info();
    assert(p2 != NULL);
    cprintf("成功分配 10 页 (应获得一个 16 页的块), 物理地址: 0x%lx\n", page2pa(p2));
    assert(nr_free_pages() == total_free_start - 8 - 16);


    cprintf("现在释放刚才分配的 8 页块...\n");
    free_pages(p1, 5); // 释放时用原始请求大小，内部会计算出阶数
    print_buddy_info();
    assert(nr_free_pages() == total_free_start - 16);

    cprintf("现在释放刚才分配的 16 页块...\n");
    free_pages(p2, 10);
    print_buddy_info();
    assert(nr_free_pages() == total_free_start);
    cprintf("测试 1 通过! 内存已全部归还。\n");


    // -------------------------------------------------
    // 测试 2: 内存块分裂
    // -------------------------------------------------
    cprintf("\n[测试 2: 内存块分裂]\n");
    cprintf("请求 129 页, 这应该会触发一个大内存块的连续分裂...\n");
    //cprintf("1111111111111111\n");
    size_t a=nr_free_pages();
    cprintf("%d\n", (int)a);
    p1 = alloc_pages(129); // alloc_page() 等于 alloc_pages(1)
    assert(p1 != NULL);
    cprintf("成功分配 129 页, 物理地址: 0x%lx\n", page2pa(p1));
    cprintf("此时空闲链表应该包含分裂后剩下的各种大小的伙伴块。\n");
    print_buddy_info();

    cprintf("2222222222222\n");
    size_t b=nr_free_pages();
    cprintf("%d\n", (int)b);
    assert(nr_free_pages() == total_free_start - 256);
    
    cprintf("释放这 129 页, 应该会触发连续合并, 最终恢复到初始状态...\n");
    free_pages(p1, 129);
    cprintf("3333333333333\n");
    size_t c=nr_free_pages();
    cprintf("%d\n", (int)c);
    assert(nr_free_pages() == total_free_start);
    cprintf("测试 2 通过! 系统状态已恢复。\n");
    print_buddy_info();


    // -------------------------------------------------
    // 测试 3: 伙伴块合并
    // -------------------------------------------------
    cprintf("\n[测试 3: 伙伴块合并]\n");
    print_buddy_info();
    cprintf("  -> 步骤 1: 制造测试环境。\n");
    cprintf("     申请一个 4 页的块(p_base)。这将分裂现有的 8 页块。\n");
    struct Page *p_base = alloc_pages(4);
    assert(p_base != NULL);
    cprintf("     现在 free_areas[2] 中应该有 p_base 的 4 页伙伴。\n");
    print_buddy_info();

    cprintf("  -> 步骤 2: 进一步分裂 p_base，为精确测试做准备。\n");
    cprintf("     先将 p_base 释放，它会与伙伴合并回 8 页块。\n");
    free_pages(p_base, 4);
    print_buddy_info();
    cprintf("     现在我们能确保，下一次分配将从这个 8 页块开始。\n");
    
    cprintf("  -> 步骤 3: 从 8 页块中分配 p1 (2页)。\n");
    p1 = alloc_pages(2);
    print_buddy_info();
    cprintf("     p1 的物理地址: 0x%lx\n", page2pa(p1));

    cprintf("  -> 步骤 4: 分配 p2 (2页)，它应该是 p1 的伙伴。\n");
    p2 = alloc_pages(2);
    print_buddy_info();
    // p1 和 p2 是从同一个 4 页块分裂的，所以地址必然是连续的
    assert(p2 == p1 + 2); 
    cprintf("     p2 的物理地址: 0x%lx\n", page2pa(p2));

    cprintf("  -> 步骤 5: 释放 p1，此时 p2 仍被占用，无法合并。\n");
    free_pages(p1, 2);
    print_buddy_info();

    cprintf("  -> 步骤 6: 释放 p2，触发合并！\n");
    cprintf("     p2 会找到空闲的 p1 并合并成 4 页块。\n");
    free_pages(p2, 2);
    print_buddy_info();

    cprintf("  -> 步骤 7: 验证合并结果。\n");
    cprintf("     重新申请 4 页，应该能得到由 p1 和 p2 合并成的新块。\n");
    struct Page *p3 = alloc_pages(4);
    print_buddy_info();
    assert(p3 == p1); // 合并后的块，头指针是地址较小的 p1
    cprintf("     成功申请到 4 页的块 @ 0x%lx, 验证成功！\n", page2pa(p3));
    free_pages(p3, 4);
    print_buddy_info();
    
    assert(nr_free_pages() == total_free_start);
    cprintf("测试 3 通过!\n");

    // -------------------------------------------------
    // 测试 4: 递归合并 (动态伙伴查找版)
    // -------------------------------------------------
    cprintf("\n[测试 4: 递归合并]\n");
    struct Page *temp=alloc_page();
    print_buddy_info();

    struct Page *pg[8];
    cprintf("  -> 步骤 1: 申请 8 个 1 页的块。\n");
    for(int i=0; i<8; ++i) {
        pg[i] = alloc_page();
        assert(pg[i] != NULL);
        cprintf("     pg[%d] -> 0x%lx (索引: %d)\n", i, page2pa(pg[i]), pg[i] - buddy_base);
    }
    print_buddy_info();
    cprintf("\n  -> 步骤 2: 全部释放这 8 个块。\n");
    cprintf("     无论它们的伙伴关系如何，正确的 buddy 算法应能处理所有合并，最终完全回收内存。\n");
    for(int i=0; i<8; ++i) {
        cprintf(" pg[%d]被释放\n", i);
        free_page(pg[i]);
        print_buddy_info();
    }
    free_page(temp);
    // 最终断言：检查总空闲页数是否恢复到了测试开始时的状态
    assert(nr_free_pages() == total_free_start);
    cprintf("测试 4 通过! 内存已完全恢复。\n");
    print_buddy_info();

    // -------------------------------------------------
    // 测试 5: 压力与碎片化测试
    // (这个测试的设计思想就是混乱，所以它不依赖于初始内存布局，无需修改)
    // -------------------------------------------------
    cprintf("\n[测试 5: 压力与碎片化测试]\n");
    struct Page *pages[10];
    cprintf("  -> 分配一系列不同大小的块...\n");
    pages[0] = alloc_pages(1); pages[1] = alloc_pages(2); pages[2] = alloc_pages(3);
    pages[3] = alloc_pages(4); pages[4] = alloc_pages(5);
    print_buddy_info();

    cprintf("  -> 交叉释放，制造内存'空洞'...\n");
    free_pages(pages[1], 2);
    free_pages(pages[3], 4);
    print_buddy_info();

    cprintf("  -> 尝试在'空洞'中再分配...\n");
    pages[5] = alloc_pages(1);
    pages[6] = alloc_pages(2);
    assert(pages[5] != NULL && pages[6] != NULL);
    print_buddy_info();

    cprintf("  -> 清理所有剩余的块，验证内存能否完全回收...\n");
    free_pages(pages[0], 1); free_pages(pages[2], 3); free_pages(pages[4], 5);
    free_pages(pages[5], 1); free_pages(pages[6], 2);
    print_buddy_info();
    
    assert(nr_free_pages() == total_free_start);
    cprintf("测试 5 通过! 所有内存已回收。\n");

    // -------------------------------------------------
    // 最终检查
    // -------------------------------------------------
    cprintf("\n--- Buddy System Check 全部通过 ---\n\n");
}

/*  
 * 定义默认物理内存管理器结构体  
 * (Define the default physical memory manager structure.)
 */
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
