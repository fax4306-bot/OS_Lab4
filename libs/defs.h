#ifndef __LIBS_DEFS_H__
#define __LIBS_DEFS_H__

/* --- 空指针定义 --- */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* --- 编译器属性宏 --- */
// 强制内联：告诉编译器无论优化等级如何，都必须将此函数内联展开
#define __always_inline inline __attribute__((always_inline))
// 禁止内联：告诉编译器不要将此函数内联
#define __noinline __attribute__((noinline))
// 无返回：告诉编译器此函数不会返回（例如 panic 或 死循环），有助于优化和消除警告
#define __noreturn __attribute__((noreturn))

/* Represents true-or-false values */
/* 布尔类型定义 */
typedef int bool;

/* Explicitly-sized versions of integer types */
/* 显式指定位宽的整数类型 */
typedef char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

/* 
 * 根据 RISC-V 架构的位宽 (__riscv_xlen) 定义通用的字长整数类型 
 * 如果是 RV64，uint_t 就是 64 位；如果是 RV32，uint_t 就是 32 位。
 */
#if __riscv_xlen == 64
  typedef uint64_t uint_t;
  typedef int64_t sint_t;
#elif __riscv_xlen == 32
  typedef uint32_t uint_t;
  typedef int32_t sint_t;
#endif

/* *
 * Pointers and addresses are 32 bits long.
 * We use pointer types to represent addresses,
 * uintptr_t to represent the numerical values of addresses.
 * */
/* 
 * 指针整数类型
 * intptr_t 和 uintptr_t 保证其长度与指针的长度相同。
 * 在进行地址计算（加减偏移量）或将指针转换为整数时使用。
 */
typedef sint_t intptr_t;
typedef uint_t uintptr_t;

/* size_t is used for memory object sizes */
/* size_t 用于表示内存对象的大小 (无符号) */
typedef uintptr_t size_t;

/* used for page numbers */
/* 物理页号 (Physical Page Number) 类型 */
typedef size_t ppn_t;

/* *
 * Rounding operations (efficient when n is a power of 2)
 * Round down to the nearest multiple of n
 * */
/* 
 * 向下取整宏
 * 将 a 向下舍入到 n 的倍数 (注意：n 必须是 2 的幂)
 * 原理：a - (a % n)
 */
#define ROUNDDOWN(a, n) ({                                          \
            size_t __a = (size_t)(a);                               \
            (typeof(a))(__a - __a % (n));                           \
        })

/* Round up to the nearest multiple of n */
/* 
 * 向上取整宏
 * 将 a 向上舍入到 n 的倍数 (注意：n 必须是 2 的幂)
 * 原理：ROUNDDOWN(a + n - 1, n)
 * 例如：ROUNDUP(1, 4) -> ROUNDDOWN(1+3, 4) -> 4
 */
#define ROUNDUP(a, n) ({                                            \
            size_t __n = (size_t)(n);                               \
            (typeof(a))(ROUNDDOWN((size_t)(a) + __n - 1, __n));     \
        })

/* Return the offset of 'member' relative to the beginning of a struct type */
/* 
 * 计算成员偏移量
 * 计算成员 member 在结构体 type 中的字节偏移量。
 * 原理：将 0 地址强制转换为 type* 指针，然后访问其 member，取该 member 的地址。
 */
#define offsetof(type, member)                                      \
    ((size_t)(&((type *)0)->member))

/* *
 * to_struct - get the struct from a ptr
 * @ptr:    a struct pointer of member
 * @type:   the type of the struct this is embedded in
 * @member: the name of the member within the struct
 * */
/* 
 * 根据成员指针获取结构体指针 (Container_of 机制)
 * @ptr:    指向结构体成员变量的指针
 * @type:   结构体的类型
 * @member: 结构体中该成员变量的名称
 * 
 * 原理：成员的地址 - 成员在结构体中的偏移量 = 结构体的起始地址
 * 用途：在链表操作中，通常只拿到 list_entry_t 的指针，需要通过这个宏找到包含它的 Page 或 proc_struct。
 */
#define to_struct(ptr, type, member)                               \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#endif /* !__LIBS_DEFS_H__ */