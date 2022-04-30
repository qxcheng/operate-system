#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "kernel/bitmap.h"
#include "kernel/list.h"

/* 虚拟地址池，用于虚拟地址管理 */
struct virtual_addr {
    struct bitmap vaddr_bitmap;  // 虚拟地址用到的位图结构
    uint32_t vaddr_start;        // 虚拟地址起始地址
};

enum pool_flags {
    PF_KERNEL = 1,  // 内核内存池
    PF_USER = 2     // 用户内存池
};

/* 内存块 */
struct mem_block {
    struct list_elem free_elem;
};

/* 内存块描述符 */
struct mem_block_desc {
    uint32_t block_size;        // 内存块大小
    uint32_t blocks_per_arena;  // 本 arena 中可容纳此 mem_block 的数量
    struct list free_list;      // 目前可用的 mem block 链表
};

#define DESC_CNT 7  // 内存块描述符个数


# define PG_P_1 1   // 第0位P值=1, 表示此页内存已存在
# define PG_P_0 0

# define PG_RW_R 0  // 第1位RW=0，表示此页内存允许读、执行，
# define PG_RW_W 2  // 第1位RW=1，表示此页内存允许读、写、执行

# define PG_US_S 0  // 第2位US=0，表示此页内存只允许特权级0、1、2的程序访问
# define PG_US_U 4  // 第2位US=1，表示此页内存允许所有特权级访问

extern struct pool kernel_pool, user_pool;
void mem_init(void);
void* get_kernel_pages(uint32_t pg_cnt);
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void malloc_init(void);
uint32_t* pte_ptr(uint32_t vaddr);
uint32_t* pde_ptr(uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void* get_a_page(enum pool_flags pf, uint32_t vaddr);
void* get_user_pages(uint32_t pg_cnt);
void block_desc_init(struct mem_block_desc* desc_array);
void* sys_malloc(uint32_t size);
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt);
void pfree(uint32_t pg_phy_addr);
void sys_free(void* ptr);

#endif