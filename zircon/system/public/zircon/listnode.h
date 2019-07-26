// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_LISTNODE_H_
#define SYSROOT_ZIRCON_LISTNODE_H_

#include <stdbool.h>
#include <stddef.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

#define containerof(ptr, type, member) ((type*)((uintptr_t)(ptr)-offsetof(type, member)))

typedef struct list_node list_node_t;

struct list_node {
  list_node_t* prev;
  list_node_t* next;
};

#define LIST_INITIAL_VALUE(list) \
  { &(list), &(list) }
#define LIST_INITIAL_CLEARED_VALUE \
  { NULL, NULL }

static inline void list_initialize(list_node_t* list) { list->prev = list->next = list; }

static inline void list_clear_node(list_node_t* item) { item->prev = item->next = 0; }

static inline bool list_in_list(const list_node_t* item) {
  if (item->prev == 0 && item->next == 0)
    return false;
  else
    return true;
}

static inline void list_add_head(list_node_t* list, list_node_t* item) {
  item->next = list->next;
  item->prev = list;
  list->next->prev = item;
  list->next = item;
}

#define list_add_after(entry, new_entry) list_add_head(entry, new_entry)

static inline void list_add_tail(list_node_t* list, list_node_t* item) {
  item->prev = list->prev;
  item->next = list;
  list->prev->next = item;
  list->prev = item;
}

#define list_add_before(entry, new_entry) list_add_tail(entry, new_entry)

static inline void list_delete(list_node_t* item) {
  item->next->prev = item->prev;
  item->prev->next = item->next;
  item->prev = item->next = 0;
}

static inline void list_replace_node(list_node_t* old_node, list_node_t* new_node) {
  // replace a spot in a list with a new node
  // assumes old_node is part of a list and new_node is not
  new_node->next = old_node->next;
  new_node->prev = old_node->prev;
  old_node->prev = old_node->next = 0;

  new_node->next->prev = new_node;
  new_node->prev->next = new_node;
}

static inline list_node_t* list_remove_head(list_node_t* list) {
  if (list->next != list) {
    list_node_t* item = list->next;
    list_delete(item);
    return item;
  } else {
    return NULL;
  }
}

#define list_remove_head_type(list, type, element) \
  ({                                               \
    list_node_t* __nod = list_remove_head(list);   \
    type* __t;                                     \
    if (__nod)                                     \
      __t = containerof(__nod, type, element);     \
    else                                           \
      __t = (type*)0;                              \
    __t;                                           \
  })

static inline list_node_t* list_remove_tail(list_node_t* list) {
  if (list->prev != list) {
    list_node_t* item = list->prev;
    list_delete(item);
    return item;
  } else {
    return NULL;
  }
}

#define list_remove_tail_type(list, type, element) \
  ({                                               \
    list_node_t* __nod = list_remove_tail(list);   \
    type* __t;                                     \
    if (__nod)                                     \
      __t = containerof(__nod, type, element);     \
    else                                           \
      __t = (type*)0;                              \
    __t;                                           \
  })

static inline list_node_t* list_peek_head(const list_node_t* list) {
  if (list->next != list) {
    return list->next;
  } else {
    return NULL;
  }
}

#define list_peek_head_type(list, type, element) \
  ({                                             \
    list_node_t* __nod = list_peek_head(list);   \
    type* __t;                                   \
    if (__nod)                                   \
      __t = containerof(__nod, type, element);   \
    else                                         \
      __t = (type*)0;                            \
    __t;                                         \
  })

static inline list_node_t* list_peek_tail(const list_node_t* list) {
  if (list->prev != list) {
    return list->prev;
  } else {
    return NULL;
  }
}

#define list_peek_tail_type(list, type, element) \
  ({                                             \
    list_node_t* __nod = list_peek_tail(list);   \
    type* __t;                                   \
    if (__nod)                                   \
      __t = containerof(__nod, type, element);   \
    else                                         \
      __t = (type*)0;                            \
    __t;                                         \
  })

static inline list_node_t* list_prev(list_node_t* list, list_node_t* item) {
  if (item->prev != list)
    return item->prev;
  else
    return NULL;
}

#define list_prev_type(list, item, type, element) \
  ({                                              \
    list_node_t* __nod = list_prev(list, item);   \
    type* __t;                                    \
    if (__nod)                                    \
      __t = containerof(__nod, type, element);    \
    else                                          \
      __t = (type*)0;                             \
    __t;                                          \
  })

static inline list_node_t* list_prev_wrap(list_node_t* list, list_node_t* item) {
  if (item->prev != list)
    return item->prev;
  else if (item->prev->prev != list)
    return item->prev->prev;
  else
    return NULL;
}

#define list_prev_wrap_type(list, item, type, element) \
  ({                                                   \
    list_node_t* __nod = list_prev_wrap(list, item);   \
    type* __t;                                         \
    if (__nod)                                         \
      __t = containerof(__nod, type, element);         \
    else                                               \
      __t = (type*)0;                                  \
    __t;                                               \
  })

static inline list_node_t* list_next(list_node_t* list, list_node_t* item) {
  if (item->next != list)
    return item->next;
  else
    return NULL;
}

#define list_next_type(list, item, type, element) \
  ({                                              \
    list_node_t* __nod = list_next(list, item);   \
    type* __t;                                    \
    if (__nod)                                    \
      __t = containerof(__nod, type, element);    \
    else                                          \
      __t = (type*)0;                             \
    __t;                                          \
  })

static inline list_node_t* list_next_wrap(list_node_t* list, list_node_t* item) {
  if (item->next != list)
    return item->next;
  else if (item->next->next != list)
    return item->next->next;
  else
    return NULL;
}

#define list_next_wrap_type(list, item, type, element) \
  ({                                                   \
    list_node_t* __nod = list_next_wrap(list, item);   \
    type* __t;                                         \
    if (__nod)                                         \
      __t = containerof(__nod, type, element);         \
    else                                               \
      __t = (type*)0;                                  \
    __t;                                               \
  })

// iterates over the list, node should be list_node_t*
#define list_for_every(list, node) for (node = (list)->next; node != (list); node = node->next)

// iterates over the list in a safe way for deletion of current node
// node and temp_node should be list_node_t*
#define list_for_every_safe(list, node, temp_node)                    \
  for (node = (list)->next, temp_node = (node)->next; node != (list); \
       node = temp_node, temp_node = (node)->next)

// iterates over the list, entry should be the container structure type *
#define list_for_every_entry(list, entry, type, member)                               \
  for ((entry) = containerof((list)->next, type, member); &(entry)->member != (list); \
       (entry) = containerof((entry)->member.next, type, member))

// iterates over the list in a safe way for deletion of current node
// entry and temp_entry should be the container structure type *
#define list_for_every_entry_safe(list, entry, temp_entry, type, member) \
  for (entry = containerof((list)->next, type, member),                  \
      temp_entry = containerof((entry)->member.next, type, member);      \
       &(entry)->member != (list);                                       \
       entry = temp_entry, temp_entry = containerof((temp_entry)->member.next, type, member))

static inline bool list_is_empty(const list_node_t* list) {
  return (list->next == list) ? true : false;
}

static inline size_t list_length(const list_node_t* list) {
  size_t cnt = 0;
  const list_node_t* node = list;
  list_for_every(list, node) { cnt++; }

  return cnt;
}

// Splice the contents of splice_from into the list immediately following pos.
static inline void list_splice_after(list_node_t* splice_from, list_node_t* pos) {
  if (list_is_empty(splice_from)) {
    return;
  }
  splice_from->next->prev = pos;
  splice_from->prev->next = pos->next;
  pos->next->prev = splice_from->prev;
  pos->next = splice_from->next;
  list_initialize(splice_from);
}

// Split the contents of list after (but not including) pos, into split_to
// (which should be empty).
static inline void list_split_after(list_node_t* list, list_node_t* pos, list_node_t* split_to) {
  if (pos->next == list) {
    list_initialize(split_to);
    return;
  }
  split_to->prev = list->prev;
  split_to->prev->next = split_to;
  split_to->next = pos->next;
  split_to->next->prev = split_to;
  pos->next = list;
  list->prev = pos;
}

// Moves all the contents of old_list (which may or may not be empty)
// to new_list (which should be empty).
static inline void list_move(list_node_t* old_list, list_node_t* new_list) {
  list_initialize(new_list);
  list_splice_after(old_list, new_list);
}

__END_CDECLS

#endif  // SYSROOT_ZIRCON_LISTNODE_H_
