/*
 * Copyright (C) 2008-2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _CUTILS_LIST_H_
#define _CUTILS_LIST_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct listnode
{
    struct listnode *next;
    struct listnode *prev;
};

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define node_to_item(node, container, member) \
    (container *) (((char*) (node)) - offsetof(container, member))

#define list_declare(name) \
    struct listnode name = { \
        .next = &name, \
        .prev = &name, \
    }

#define list_for_each(node, list) \
    for (node = (list)->next; node != (list); node = node->next)

#define list_for_each_reverse(node, list) \
    for (node = (list)->prev; node != (list); node = node->prev)

#define list_for_each_safe(node, n, list) \
    for (node = (list)->next, n = node->next; \
         node != (list); \
         node = n, n = node->next)

static inline void list_init(struct listnode *node)
{
    node->next = node;
    node->prev = node;
}

static inline void INIT_LIST_HEAD(struct listnode *list)
{
    list->next = list;
    list->prev = list;
}

static inline void list_add_tail(struct listnode *head, struct listnode *item)
{
    item->next = head;
    item->prev = head->prev;
    head->prev->next = item;
    head->prev = item;
}

static inline void list_add_head(struct listnode *head, struct listnode *item)
{
    item->next = head->next;
    item->prev = head;
    head->next->prev = item;
    head->next = item;
}

static inline void list_remove(struct listnode *item)
{
    item->next->prev = item->prev;
    item->prev->next = item->next;
}

static inline int list_count(struct listnode *head)
{
    int ret = 0;
    struct listnode *node;
    list_for_each(node, head) {
        ret++;
    }
    return ret;
}


#define list_empty(list) ((list) == (list)->next)
#define list_head(list) ((list)->next)
#define list_tail(list) ((list)->prev)

#ifndef container_of
#if defined(__GNUC__) /* __GNUC__ */
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
                const __typeof__( ((type *)0)->member ) *__mptr = (ptr);    \
                (type *)( (char *)__mptr - offsetof(type,member) );})

#elif defined(_MSC_VER) && (_MSC_VER >= 1200 ) /* vc 6.0 + */
#define container_of(ptr, type, member) \
     (type *)( (char *)(ptr) - offsetof(type, member)  )
#endif
#endif

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define list_for_each_entry(pos, head, member)              \
    for (pos = list_entry((head)->next, typeof(*pos), member);  \
        &pos->member != (head);    \
        pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_reverse(pos, head, member)           \
    for (pos = list_entry((head)->prev, typeof(*pos), member);  \
    &pos->member != (head);    \
    pos = list_entry(pos->member.prev, typeof(*pos), member))

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif
