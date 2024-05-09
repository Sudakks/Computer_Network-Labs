#ifndef __LIST_H__
#define __LIST_H__

#include <stddef.h>

struct list_head {
	struct list_head *next, *prev;
};

#define list_empty(list) ((list)->next == (list))

//从一个结构体中的成员指针获取整个结构体的指针
//ptr：指向结构体中某个成员的指针
//member：表示结构体中作为链表成员的指针
#define list_entry(ptr, type, member) \
	(type *)((char *)ptr - offsetof(type, member))

//它是用于遍历链表的一个迭代器，这个宏可以在链表中遍历每个结构体元素，并执行用户定义的操作
//初始化 pos，将其设为指向链表中第一个元素的指针
//终止条件:在 pos 指针所指向的结构体的成员指针不等于链表头指针时继续循环
//它这里应该是循环链表
//循环迭代的步骤:它将 pos 指针更新为下一个链表元素的指针
//使用 list_for_each_entry 宏来遍历给定链表，通过 pos 变量表示当前结构体指针，并在循环体中对每个元素执行操作
#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member); \
			&pos->member != (head); \
			pos = list_entry(pos->member.next, typeof(*pos), member)) 

//类似于上面的便利，但是提供了一种在遍历链表的过程中安全删除元素的机制，可以安全删除pos
//pos是遍历过程中当前结构体的指针
//q:用于安全删除时临时保存下一个结构体指针的变量
//初始化：pos 被设置为链表中第一个元素的指针，q 被设置为下一个元素的指针

//pos相当于元素位置
//member相当于node里面的next
#define list_for_each_entry_safe(pos, q, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member), \
			q = list_entry(pos->member.next, typeof(*pos), member); \
			&pos->member != (head); \
			pos = q, q = list_entry(pos->member.next, typeof(*q), member))

static inline void init_list_head(struct list_head *list)
{
	list->next = list->prev = list;
}

static inline void list_insert(struct list_head *new,
		struct list_head *prev,
		struct list_head *next)
{
	next->prev = new;
	prev->next = new;
	new->next = next;
	new->prev = prev;
}

static inline void list_delete_entry(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	list_insert(new, head->prev, head);
}

#endif
