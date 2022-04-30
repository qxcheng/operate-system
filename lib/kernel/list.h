# ifndef _LIB_KERNEL_LIST_H
# define _LIB_KERNEL_LIST_H

# include "global.h"

/**
 * 获取结构体内成员在结构体的偏移.
 */ 
# define offset(struct_type, member) (int) (&((struct_type*)0)->member)

/* 将指针 elem_ptr 转换成 struct_type 类型的指针，其原理是用
   elem_ptr的地址减去 elem_ptr在结构体 struct_type 中的偏移量，
   此地址差便是结构体 struct_type 的起始地址，
   最后再将此地址差转换为 struct_type 指针类型 */
# define elem2entry(struct_type, struct_member_name, elem_ptr) \
    (struct_type*) ((int) elem_ptr - offset(struct_type, struct_member_name))

/**
 * 链表节点.
 */ 
struct list_elem {
    struct list_elem* prev;  // 前驱节点
    struct list_elem* next;  // 后继节点
};

/**
 * 链表结构.
 */ 
struct list {
    struct list_elem head;  // 队首，固定不变，第一个元素为head.next
    struct list_elem tail;  // 队尾，固定不变，tail.prev是最后一个结点
};

/**
 * 用于链表遍历的回调函数.
 */ 
typedef bool (function) (struct list_elem*, int arg);

void list_init (struct list*);
void list_insert_before(struct list_elem* before, struct list_elem* elem);
void list_push(struct list* plist, struct list_elem* elem);
void list_iterate(struct list* plist);
void list_append(struct list* plist, struct list_elem* elem);  
void list_remove(struct list_elem* pelem);
struct list_elem* list_pop(struct list* plist);
bool list_empty(struct list* plist);
uint32_t list_len(struct list* plist);
struct list_elem* list_traversal(struct list* plist, function func, int arg);
bool elem_find(struct list* plist, struct list_elem* obj_elem);

# endif