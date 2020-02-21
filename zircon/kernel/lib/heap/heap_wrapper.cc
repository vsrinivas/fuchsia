// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <lib/cmpctmalloc.h>
#include <lib/console.h>
#include <lib/heap.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/listnode.h>

#include <arch/ops.h>
#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

#ifndef HEAP_PANIC_ON_ALLOC_FAIL
#if LK_DEBUGLEVEL > 2
#define HEAP_PANIC_ON_ALLOC_FAIL 1
#else
#define HEAP_PANIC_ON_ALLOC_FAIL 0
#endif
#endif

/* heap tracing */
#if LK_DEBUGLEVEL > 1
static bool heap_trace = false;
#else
#define heap_trace (false)
#endif

// keep a list of unique caller:size sites in a list
namespace {

struct alloc_stat {
  list_node node;

  void* caller;
  size_t size;

  uint64_t count;
};

const size_t num_stats = 1024;
size_t next_unused_stat = 0;
alloc_stat stats[num_stats];

list_node stat_list = LIST_INITIAL_VALUE(stat_list);
SpinLock stat_lock;

void add_stat(void* caller, size_t size) {
  if (!HEAP_COLLECT_STATS) {
    return;
  }

  AutoSpinLock guard(&stat_lock);

  // look for an existing stat, bump the count and move to head if found
  alloc_stat* s;
  list_for_every_entry (&stat_list, s, alloc_stat, node) {
    if (s->caller == caller && s->size == size) {
      s->count++;
      list_delete(&s->node);
      list_add_head(&stat_list, &s->node);
      return;
    }
  }

  // allocate a new one and add it to the list
  if (unlikely(next_unused_stat >= num_stats))
    return;

  s = &stats[next_unused_stat++];
  s->caller = caller;
  s->size = size;
  s->count = 1;
  list_add_head(&stat_list, &s->node);
}

void dump_stats() {
  if (!HEAP_COLLECT_STATS) {
    return;
  }

  AutoSpinLock guard(&stat_lock);

  // remove all of them from the list
  alloc_stat* s;
  while ((s = list_remove_head_type(&stat_list, alloc_stat, node)))
    ;

  // reinsert all of the entries, sorted by size
  for (size_t i = 0; i < next_unused_stat; i++) {
    bool added = false;
    list_for_every_entry (&stat_list, s, alloc_stat, node) {
      if (stats[i].size >= s->size) {
        list_add_before(&s->node, &stats[i].node);
        added = true;
        break;
      }
    }
    // fell off the end
    if (!added) {
      list_add_tail(&stat_list, &stats[i].node);
    }
  }

  // dump the list of stats
  list_for_every_entry (&stat_list, s, alloc_stat, node) {
    printf("size %8zu count %8" PRIu64 " caller %p\n", s->size, s->count, s->caller);
  }

  if (next_unused_stat >= num_stats) {
    printf("WARNING: max number of unique records hit, some statistics were likely lost\n");
  }
}

}  // namespace

void heap_init() { cmpct_init(); }

void heap_trim() { cmpct_trim(); }

void* malloc(size_t size) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("size %zu\n", size);

  add_stat(__GET_CALLER(), size);

  void* ptr = cmpct_alloc(size);
  if (unlikely(heap_trace)) {
    printf("caller %p malloc %zu -> %p\n", __GET_CALLER(), size, ptr);
  }

  if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr)) {
    panic("malloc of size %zu failed\n", size);
  }

  return ptr;
}

void* malloc_debug_caller_(size_t size, void* caller) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("size %zu\n", size);

  add_stat(caller, size);

  void* ptr = cmpct_alloc(size);
  if (unlikely(heap_trace)) {
    printf("caller %p malloc %zu -> %p\n", caller, size, ptr);
  }

  if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr)) {
    panic("malloc of size %zu failed\n", size);
  }

  return ptr;
}

void* memalign_debug_caller_(size_t size, size_t align, void* caller) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("size %zu\n", size);

  add_stat(caller, size);

  void* ptr = cmpct_memalign(size, align);
  if (unlikely(heap_trace)) {
    printf("caller %p malloc %zu -> %p\n", caller, size, ptr);
  }

  if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr)) {
    panic("malloc of size %zu failed\n", size);
  }

  return ptr;
}

void* memalign(size_t boundary, size_t size) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("boundary %zu, size %zu\n", boundary, size);

  add_stat(__GET_CALLER(), size);

  void* ptr = cmpct_memalign(size, boundary);
  if (unlikely(heap_trace)) {
    printf("caller %p memalign %zu, %zu -> %p\n", __GET_CALLER(), boundary, size, ptr);
  }

  if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr)) {
    panic("memalign of size %zu align %zu failed\n", size, boundary);
  }

  return ptr;
}

void* calloc(size_t count, size_t size) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("count %zu, size %zu\n", count, size);

  add_stat(__GET_CALLER(), size);

  size_t realsize = count * size;

  void* ptr = cmpct_alloc(realsize);
  if (likely(ptr)) {
    memset(ptr, 0, realsize);
  }
  if (unlikely(heap_trace)) {
    printf("caller %p calloc %zu, %zu -> %p\n", __GET_CALLER(), count, size, ptr);
  }
  return ptr;
}

void* realloc(void* ptr, size_t size) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("ptr %p, size %zu\n", ptr, size);

  add_stat(__GET_CALLER(), size);

  void* ptr2 = cmpct_realloc(ptr, size);
  if (unlikely(heap_trace)) {
    printf("caller %p realloc %p, %zu -> %p\n", __GET_CALLER(), ptr, size, ptr2);
  }

  if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr2)) {
    panic("realloc of size %zu old ptr %p failed\n", size, ptr);
  }

  return ptr2;
}

void free(void* ptr) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("ptr %p\n", ptr);
  if (unlikely(heap_trace)) {
    printf("caller %p free %p\n", __GET_CALLER(), ptr);
  }

  cmpct_free(ptr);
}

static void heap_dump(bool panic_time) { cmpct_dump(panic_time); }

void heap_get_info(size_t* size_bytes, size_t* free_bytes) {
  cmpct_get_info(size_bytes, free_bytes);
}

static void heap_test() { cmpct_test(); }

void* heap_page_alloc(size_t pages) {
  DEBUG_ASSERT(pages > 0);

  list_node list = LIST_INITIAL_VALUE(list);

  paddr_t pa;
  zx_status_t status = pmm_alloc_contiguous(pages, 0, PAGE_SIZE_SHIFT, &pa, &list);
  if (status != ZX_OK) {
    return nullptr;
  }

  // mark all of the allocated page as HEAP
  vm_page_t *p, *temp;
  list_for_every_entry_safe (&list, p, temp, vm_page_t, queue_node) {
    list_delete(&p->queue_node);
    p->set_state(VM_PAGE_STATE_HEAP);
  }

  LTRACEF("pages %zu: pa %#lx, va %p\n", pages, pa, paddr_to_physmap(pa));

  return paddr_to_physmap(pa);
}

void heap_page_free(void* _ptr, size_t pages) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED((uintptr_t)_ptr));
  DEBUG_ASSERT(pages > 0);

  LTRACEF("ptr %p, pages %zu\n", _ptr, pages);

  uint8_t* ptr = (uint8_t*)_ptr;

  list_node list;
  list_initialize(&list);

  while (pages > 0) {
    vm_page_t* p = paddr_to_vm_page(vaddr_to_paddr(ptr));
    if (p) {
      DEBUG_ASSERT(p->state() == VM_PAGE_STATE_HEAP);
      DEBUG_ASSERT(!list_in_list(&p->queue_node));

      list_add_tail(&list, &p->queue_node);
    }

    ptr += PAGE_SIZE;
    pages--;
  }

  pmm_free(&list);
}

#if LK_DEBUGLEVEL > 1

#include <lib/console.h>

static int cmd_heap(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("heap", "heap debug commands", &cmd_heap, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(heap)

static int cmd_heap(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  usage:
    printf("usage:\n");
    printf("\t%s info\n", argv[0].str);
    if (HEAP_COLLECT_STATS) {
      printf("\t%s stats\n", argv[0].str);
    }
    if (!(flags & CMD_FLAG_PANIC)) {
      printf("\t%s trace\n", argv[0].str);
      printf("\t%s trim\n", argv[0].str);
      printf("\t%s test\n", argv[0].str);
    }
    return -1;
  }

  if (strcmp(argv[1].str, "info") == 0) {
    heap_dump(flags & CMD_FLAG_PANIC);
  } else if (HEAP_COLLECT_STATS && strcmp(argv[1].str, "stats") == 0) {
    dump_stats();
  } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "test") == 0) {
    heap_test();
  } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "trace") == 0) {
    heap_trace = !heap_trace;
    printf("heap trace is now %s\n", heap_trace ? "on" : "off");
  } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "trim") == 0) {
    heap_trim();
  } else {
    printf("unrecognized command\n");
    goto usage;
  }

  return 0;
}

#endif
