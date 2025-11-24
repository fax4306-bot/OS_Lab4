// kern/mm/buddy_pmm.h
#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

extern const struct pmm_manager buddy_pmm_manager;

#endif /* !__KERN_MM_BUDDY_PMM_H__ */

// 用于声明 buddy_pmm_manager 这个实例，以便其他文件（主要是 pmm.c）可以引用它。