#ifndef __KERN_MM_MMU_H__
#define __KERN_MM_MMU_H__

#ifndef __ASSEMBLER__
#include <defs.h>
#endif /* !__ASSEMBLER__ */

// A linear address 'la' has a four-part structure as follows:
// 线性地址 'la' (即虚拟地址) 在 Sv39 模式下拥有如下的四部分结构：
//
// Sv39 结构: 9位一级页号(VPN2) + 9位二级页号(VPN1) + 9位三级页号(VPN0) + 12位页内偏移
//
// +--------9-------+-------9--------+-------9--------+---------12----------+
// | Page Directory | Page Directory |   Page Table   | Offset within Page  |
// |     Index 1    |    Index 0     |     Index      |                     |
// |    (VPN[2])    |    (VPN[1])    |    (VPN[0])    |                     |
// +----------------+----------------+----------------+---------------------+
//  \--- PDX1(la) --/ \--- PDX0(la) --/ \--- PTX(la) --/ \---- PGOFF(la) ----/
//  \-------------------PPN(la)----------------------/
//
// PDX1, PDX0, PTX, PGOFF 和 PPN 宏用于按照上述结构分解线性地址。
// 若要通过 PDX, PTX 和 PGOFF 构造一个线性地址，请使用 PGADDR 宏。

// RISC-V 使用 39 位虚拟地址来访问 56 位物理地址！(Sv39模式)
// Sv39 page table entry:
// Sv39 页表项 (PTE) 结构 (64位)：
// +----26---+----9---+----9---+---10---+-------10-------+
// |  PPN[2] | PPN[1] | PPN[0] |Reserved| RSW |D|A|G|U|X|W|R|V|
// +---------+----+---+--------+--------+-------+----------+
// |<------- PPN (44 bits) ------->|

// page directory index 1 (VPN[2])
// 获取一级页目录索引
// 解释：右移 30 位，然后取低 9 位 (0x1FF)
#define PDX1(la) ((((uintptr_t)(la)) >> PDX1SHIFT) & 0x1FF)

// page directory index 0 (VPN[1])
// 获取二级页目录索引
// 解释：右移 21 位，然后取低 9 位
#define PDX0(la) ((((uintptr_t)(la)) >> PDX0SHIFT) & 0x1FF)

// page table index (VPN[0])
// 获取页表索引
// 解释：右移 12 位，然后取低 9 位
#define PTX(la) ((((uintptr_t)(la)) >> PTXSHIFT) & 0x1FF)

// page number field of address
// 获取地址中的页号字段 (物理页号 PPN)
// 解释：直接右移 12 位，去掉页内偏移
#define PPN(la) (((uintptr_t)(la)) >> PTXSHIFT)

// offset in page
// 获取页内偏移量
// 解释：取低 12 位 (0xFFF)
#define PGOFF(la) (((uintptr_t)(la)) & 0xFFF)

// construct linear address from indexes and offset
// 根据索引和偏移量构造线性地址 (虚拟地址)
// 解释：将各部分索引左移到对应位置，最后或上偏移量
#define PGADDR(d1, d0, t, o) ((uintptr_t)((d1) << PDX1SHIFT |(d0) << PDX0SHIFT | (t) << PTXSHIFT | (o)))

// address in page table or page directory entry
// 从页表项(PTE)或页目录项(PDE)中提取物理地址
// 解释：
// 1. ~0x3FF：清除低10位的标志位（保留位+权限位）
// 2. << (PTXSHIFT - PTE_PPN_SHIFT)：即 << (12 - 10) = << 2。
//    因为 PTE 中 PPN 从第10位开始，而物理地址中 PPN 应该左移12位（页对齐），所以还需要左移2位。
#define PTE_ADDR(pte)   (((uintptr_t)(pte) & ~0x3FF) << (PTXSHIFT - PTE_PPN_SHIFT))
#define PDE_ADDR(pde)   PTE_ADDR(pde)

/* page directory and page table constants */
/* 页目录和页表相关常量 */
#define NPDEENTRY       512                    // 每个页目录包含的条目数 (Sv39: 2^9 = 512)
#define NPTEENTRY       512                    // 每个页表包含的条目数
#define PGSIZE          4096                    // 一个页映射的字节数 (4KB)
#define PGSHIFT         12                      // 页大小的对数 (12位偏移)
#define PTSIZE          (PGSIZE * NPTEENTRY)    // 一个页目录项映射的内存大小 (2MB, 大页)
#define PTSHIFT         21                      // 大页大小的对数

#define PTXSHIFT        12                      // offset of PTX in a linear address (页表索引偏移)
#define PDX0SHIFT       21                      // offset of PDX in a linear address (二级页目录索引偏移)
#define PDX1SHIFT		30                      // (一级页目录索引偏移)
#define PTE_PPN_SHIFT   10                      // PPN 在 PTE 中的起始位偏移 (RISC-V 规定 PTE 低10位为标志位)

// page table entry (PTE) fields
// 页表项 (PTE) 的标志位定义
#define PTE_V     0x001 // Valid - 页表项是否有效
#define PTE_R     0x002 // Read - 是否可读
#define PTE_W     0x004 // Write - 是否可写
#define PTE_X     0x008 // Execute - 是否可执行
#define PTE_U     0x010 // User - 用户态是否可访问
#define PTE_G     0x020 // Global - 全局映射（TLB中不刷新）
#define PTE_A     0x040 // Accessed - 访问位（页面被访问过，硬件置1）
#define PTE_D     0x080 // Dirty - 脏位（页面被写入过，硬件置1）
#define PTE_SOFT  0x300 // Reserved for Software - 软件保留位（操作系统可用）

// 常用权限组合宏定义
#define PAGE_TABLE_DIR (PTE_V)              // 指向下一级页表的目录项 (仅有效位)
#define READ_ONLY (PTE_R | PTE_V)           // 只读
#define READ_WRITE (PTE_R | PTE_W | PTE_V)  // 读写
#define EXEC_ONLY (PTE_X | PTE_V)           // 只执行
#define READ_EXEC (PTE_R | PTE_X | PTE_V)   // 读执行 (代码段)
#define READ_WRITE_EXEC (PTE_R | PTE_W | PTE_X | PTE_V) // 读写执行

#define PTE_USER (PTE_R | PTE_W | PTE_X | PTE_U | PTE_V) // 用户态全权限

#endif /* !__KERN_MM_MMU_H__ */