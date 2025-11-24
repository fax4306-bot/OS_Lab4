#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <assert.h>
#include <kmalloc.h>
#include <sync.h>
#include <pmm.h>
#include <stdio.h>

/*
 * SLOB 分配器: 简单块列表 (Simple List Of Blocks)
 *
 * Matt Mackall <mpm@selenic.com> 12/30/03
 *
 * SLOB 的工作原理:
 *
 * SLOB 的核心是一个传统的 K&R 风格的堆分配器，支持返回对齐的对象。
 * 在 x86 上，该分配器的粒度为 8 字节。
 * SLOB 堆是一个单向链表，由 __get_free_page 获取的页面组成，按需增长。
 * 堆的分配策略目前采用首次适应算法 (First-Fit)。
 *
 * 在此之上实现了 kmalloc/kfree。
 * kmalloc 返回的块是 8 字节对齐的，并且前面有一个 8 字节的头部 (slob_t)。
 * 如果 kmalloc 请求的对象大小大于等于 PAGE_SIZE (4KB)，它会直接调用 __get_free_pages，
 * 以便返回页对齐的块，并维护一个包含这些页及其阶数(order)的链表 (bigblock)。
 * 在 kfree() 中，通过检测地址是否页对齐来识别这些大对象。
 *
 * SLAB 是在 SLOB 之上模拟的（在 uCore Lab4 中其实并没有完整实现 SLAB，主要是使用了 SLOB 的逻辑）。
 * 除非设置了 SLAB_MUST_HWCACHE_ALIGN 标志，否则对象以 8 字节对齐返回。
 * 同样，大于等于页大小的对象通过调用 __get_free_pages 分配。
 */

// 辅助宏：在 uCore 中，自旋锁保存中断状态实际上就是关中断
#define spin_lock_irqsave(l, f) local_intr_save(f)
#define spin_unlock_irqrestore(l, f) local_intr_restore(f)
typedef unsigned int gfp_t; // Get Free Page flags (虽然 uCore 这里没怎么用到)

#ifndef PAGE_SIZE
#define PAGE_SIZE PGSIZE
#endif

#ifndef L1_CACHE_BYTES
#define L1_CACHE_BYTES 64
#endif

// 对齐宏：将 addr 向上对齐到 size 的倍数
#ifndef ALIGN
#define ALIGN(addr, size) (((addr) + (size) - 1) & (~((size) - 1)))
#endif

// SLOB 块头结构体
// 每个空闲块或已分配块的前面都有这个头
struct slob_block
{
	int units;               // 该块的大小（以 SLOB_UNIT 为单位）
	struct slob_block *next; // 指向下一个空闲块的指针（仅在空闲链表中有效）
};
typedef struct slob_block slob_t;

#define SLOB_UNIT sizeof(slob_t) // 单元大小，通常是结构体的大小
// 计算 size 需要多少个单元 (向上取整)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1) / SLOB_UNIT)
#define SLOB_ALIGN L1_CACHE_BYTES

// 大块内存管理结构体 (用于管理 > 4KB 的分配)
struct bigblock
{
	int order;              // 分配的页数阶数 (2^order 页)
	void *pages;            // 指向分配页面的内核虚拟地址
	struct bigblock *next;  // 链表指针
};
typedef struct bigblock bigblock_t;

// arena 是空闲链表的哨兵节点/初始节点
static slob_t arena = {.next = &arena, .units = 1};
// slobfree 指向当前的空闲链表头
static slob_t *slobfree = &arena;
// bigblocks 指向大块内存分配链表头
static bigblock_t *bigblocks;

// 这是一个假的锁变量，因为 uCore 目前通过关中断来实现互斥
// static spinlock_t slob_lock;
// static spinlock_t block_lock;
// 我们假设这些锁已经被 "定义" 在逻辑中，使用上面的宏来操作

// 底层函数：分配 2^order 个物理页，并返回内核虚拟地址
static void *__slob_get_free_pages(gfp_t gfp, int order)
{
	struct Page *page = alloc_pages(1 << order);
	if (!page)
		return NULL;
	return page2kva(page);
}

#define __slob_get_free_page(gfp) __slob_get_free_pages(gfp, 0)

// 底层函数：释放 2^order 个物理页
static inline void __slob_free_pages(unsigned long kva, int order)
{
	free_pages(kva2page(kva), 1 << order);
}

static void slob_free(void *b, int size);

// SLOB 分配核心逻辑 (用于小内存分配)
static void *slob_alloc(size_t size, gfp_t gfp, int align)
{
	assert((size + SLOB_UNIT) < PAGE_SIZE); // 确保申请大小小于一页

	slob_t *prev, *cur, *aligned = 0;
	int delta = 0, units = SLOB_UNITS(size); // 计算需要的单元数
	unsigned long flags;

	// 1. 关中断，进入临界区
	spin_lock_irqsave(&slob_lock, flags);
	prev = slobfree;

	// 2. 遍历空闲链表 (First-Fit 策略)
	for (cur = prev->next;; prev = cur, cur = cur->next)
	{
		// 处理对齐要求
		if (align)
		{
			aligned = (slob_t *)ALIGN((unsigned long)cur, align);
			delta = aligned - cur;
		}
		// 3. 检查当前块是否足够大
		if (cur->units >= units + delta)
		{ /* 空间足够 */
			if (delta)
			{ /* 需要分割头部以满足对齐? */
				aligned->units = cur->units - delta;
				aligned->next = cur->next;
				cur->next = aligned;
				cur->units = delta;
				prev = cur;
				cur = aligned;
			}

			// 4. 执行分配：从链表中移除该块或分割该块
			if (cur->units == units)	/* 正好合适? */
				prev->next = cur->next; /* 直接断开链接 */
			else
			{ /* 当前块比需要的更大，进行分割 (Fragment) */
				prev->next = cur + units; // prev 指向剩余部分
				prev->next->units = cur->units - units; // 设置剩余部分大小
				prev->next->next = cur->next;
				cur->units = units; // 设置分配块的大小
			}

			slobfree = prev; // 更新空闲链表头指针，优化下次查找
			spin_unlock_irqrestore(&slob_lock, flags);
			return cur; // 返回找到的块
		}

		// 5. 如果遍历了一圈回到了起点，说明没有足够大的空闲块
		if (cur == slobfree)
		{
			spin_unlock_irqrestore(&slob_lock, flags); // 暂时开中断

			if (size == PAGE_SIZE) /* 试图收缩 arena? (边界情况) */
				return 0;

			// 6. 申请一个新的物理页
			cur = (slob_t *)__slob_get_free_page(gfp);
			if (!cur)
				return 0;

			// 7. 将新页作为一个大的空闲块释放回 SLOB 池中
			slob_free(cur, PAGE_SIZE);
			
			// 8. 重新关中断，从新更新的 slobfree 处继续尝试分配
			spin_lock_irqsave(&slob_lock, flags);
			cur = slobfree;
		}
	}
}

// SLOB 释放核心逻辑
static void slob_free(void *block, int size)
{
	slob_t *cur, *b = (slob_t *)block;
	unsigned long flags;

	if (!block)
		return;

	if (size)
		b->units = SLOB_UNITS(size);

	/* 1. 找到重新插入的位置 */
	// 链表是按地址排序的，需要找到 cur < b < cur->next 的位置
	spin_lock_irqsave(&slob_lock, flags);
	for (cur = slobfree; !(b > cur && b < cur->next); cur = cur->next)
		if (cur >= cur->next && (b > cur || b < cur->next))
			break;

	// 2. 尝试与后一个块合并
	if (b + b->units == cur->next)
	{
		b->units += cur->next->units;
		b->next = cur->next->next;
	}
	else
		b->next = cur->next;

	// 3. 尝试与前一个块合并
	if (cur + cur->units == b)
	{
		cur->units += b->units;
		cur->next = b->next;
	}
	else
		cur->next = b;

	slobfree = cur; // 更新空闲链表头

	spin_unlock_irqrestore(&slob_lock, flags);
}

void slob_init(void)
{
	cprintf("use SLOB allocator\n");
}

inline void
kmalloc_init(void)
{
	slob_init();
	cprintf("kmalloc_init() succeeded!\n");
}

size_t
slob_allocated(void)
{
	return 0;
}

size_t
kallocated(void)
{
	return slob_allocated();
}

// 计算 size 需要 2^order 个页
static int find_order(int size)
{
	int order = 0;
	for (; size > 4096; size >>= 1)
		order++;
	return order;
}

// kmalloc 的内部实现
static void *__kmalloc(size_t size, gfp_t gfp)
{
	slob_t *m;
	bigblock_t *bb;
	unsigned long flags;

	// 情况 1: 请求大小小于一页 (减去头部开销)
	// 使用 SLOB 分配器
	if (size < PAGE_SIZE - SLOB_UNIT)
	{
		m = slob_alloc(size + SLOB_UNIT, gfp, 0);
		// 返回头部之后的数据区域指针
		return m ? (void *)(m + 1) : 0;
	}

	// 情况 2: 请求大小大于等于一页 (大块分配)
	// 首先分配一个 bigblock 结构体来记录这个大块的信息
	bb = slob_alloc(sizeof(bigblock_t), gfp, 0);
	if (!bb)
		return 0;

	// 计算阶数并直接从 PMM 分配物理页
	bb->order = find_order(size);
	bb->pages = (void *)__slob_get_free_pages(gfp, bb->order);

	if (bb->pages)
	{
		// 将 bigblock 插入到 bigblocks 链表中进行管理
		spin_lock_irqsave(&block_lock, flags);
		bb->next = bigblocks;
		bigblocks = bb;
		spin_unlock_irqrestore(&block_lock, flags);
		return bb->pages;
	}

	slob_free(bb, sizeof(bigblock_t));
	return 0;
}

// 对外接口：分配内存
void *
kmalloc(size_t size)
{
	return __kmalloc(size, 0);
}

// 对外接口：释放内存
void kfree(void *block)
{
	bigblock_t *bb, **last = &bigblocks;
	unsigned long flags;

	if (!block)
		return;

	// 检查 block 是否页对齐
	// 如果是页对齐的，说明它是通过 __get_free_pages 分配的大块
	// (因为 slob_alloc 返回的地址是 header + 1，绝对不会页对齐)
	if (!((unsigned long)block & (PAGE_SIZE - 1)))
	{
		/* 可能在大块列表中 */
		spin_lock_irqsave(&block_lock, flags);
		for (bb = bigblocks; bb; last = &bb->next, bb = bb->next)
		{
			if (bb->pages == block)
			{
				// 从链表中移除
				*last = bb->next;
				spin_unlock_irqrestore(&block_lock, flags);
				// 释放物理页
				__slob_free_pages((unsigned long)block, bb->order);
				// 释放 bigblock 结构体本身
				slob_free(bb, sizeof(bigblock_t));
				return;
			}
		}
		spin_unlock_irqrestore(&block_lock, flags);
	}

	// 如果不是页对齐，说明是 SLOB 分配的小块
	// 倒退一个单位找到头部，然后释放
	slob_free((slob_t *)block - 1, 0);
	return;
}

// 获取分配块的大小
unsigned int ksize(const void *block)
{
	bigblock_t *bb;
	unsigned long flags;

	if (!block)
		return 0;

	// 同样通过页对齐判断是否为大块
	if (!((unsigned long)block & (PAGE_SIZE - 1)))
	{
		spin_lock_irqsave(&block_lock, flags);
		for (bb = bigblocks; bb; bb = bb->next)
			if (bb->pages == block)
			{
				spin_unlock_irqrestore(&slob_lock, flags);
				return PAGE_SIZE << bb->order;
			}
		spin_unlock_irqrestore(&block_lock, flags);
	}

	// 小块：从头部读取大小
	return ((slob_t *)block - 1)->units * SLOB_UNIT;
}