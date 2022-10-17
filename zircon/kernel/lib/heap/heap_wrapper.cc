// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <debug.h>
#include <lib/cmpctmalloc.h>
#include <lib/console.h>
#include <lib/heap.h>
#include <lib/heap_internal.h>
#include <lib/instrumentation/asan.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/virtual_alloc.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <fbl/intrusive_double_list.h>
#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
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
static bool heap_trace = false;

// keep a list of unique caller:size sites in a list
namespace {

struct alloc_stat : fbl::DoublyLinkedListable<alloc_stat*> {
  void* caller;
  size_t size;

  uint64_t count;
};

const size_t num_stats = 1024;
size_t next_unused_stat = 0;
alloc_stat stats[num_stats];

fbl::DoublyLinkedList<alloc_stat*> stat_list;
SpinLock stat_lock;
lazy_init::LazyInit<VirtualAlloc> virtual_alloc;

void add_stat(void* caller, size_t size) {
  if (!HEAP_COLLECT_STATS) {
    return;
  }

  AutoSpinLock guard(&stat_lock);

  // look for an existing stat, bump the count and move to head if found
  for (alloc_stat& s : stat_list) {
    if (s.caller == caller && s.size == size) {
      s.count++;
      stat_list.erase(s);
      stat_list.push_front(&s);
      return;
    }
  }

  // allocate a new one and add it to the list
  if (unlikely(next_unused_stat >= num_stats))
    return;

  alloc_stat* s = &stats[next_unused_stat++];
  s->caller = caller;
  s->size = size;
  s->count = 1;
  stat_list.push_front(s);
}

void dump_stats() {
  if (!HEAP_COLLECT_STATS) {
    return;
  }

  AutoSpinLock guard(&stat_lock);

  // remove all of them from the list
  stat_list.clear();

  // reinsert all of the entries, sorted by size
  for (size_t i = 0; i < next_unused_stat; i++) {
    bool added = false;
    for (alloc_stat& s : stat_list) {
      if (stats[i].size >= s.size) {
        stat_list.insert(s, &stats[i]);
        added = true;
        break;
      }
    }
    // fell off the end
    if (!added) {
      stat_list.push_back(&stats[i]);
    }
  }

  // dump the list of stats
  for (alloc_stat& s : stat_list) {
    printf("size %8zu count %8" PRIu64 " caller %p\n", s.size, s.count, s.caller);
  }

  if (next_unused_stat >= num_stats) {
    printf("WARNING: max number of unique records hit, some statistics were likely lost\n");
  }
}

}  // namespace

void heap_init() {
  if constexpr (VIRTUAL_HEAP) {
    virtual_alloc.Initialize(vm_page_state::HEAP);
    zx_status_t status = virtual_alloc->Init(vm_get_kernel_heap_base(), vm_get_kernel_heap_size(),
                                             1, ARCH_HEAP_ALIGN_BITS);
    if (status != ZX_OK) {
      panic("Failed to initialized heap backing allocator: %d", status);
    }

    printf("Kernel heap [%" PRIxPTR ", %" PRIxPTR
           ") using %zu pages (%zu KiB) for tracking bitmap\n",
           vm_get_kernel_heap_base(), vm_get_kernel_heap_base() + vm_get_kernel_heap_size(),
           virtual_alloc->DebugBitmapPages(), virtual_alloc->DebugBitmapPages() * PAGE_SIZE / 1024);
  }

  cmpct_init();
}

void* malloc(size_t size) {
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());

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

void* memalign(size_t alignment, size_t size) {
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());

  LTRACEF("alignment %zu, size %zu\n", alignment, size);

  add_stat(__GET_CALLER(), size);

  void* ptr = cmpct_memalign(alignment, size);
  if (unlikely(heap_trace)) {
    printf("caller %p memalign %zu, %zu -> %p\n", __GET_CALLER(), alignment, size, ptr);
  }

  if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr)) {
    panic("memalign of size %zu align %zu failed\n", size, alignment);
  }

  return ptr;
}

void* calloc(size_t count, size_t size) {
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());

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

void free(void* ptr) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("ptr %p\n", ptr);
  if (unlikely(heap_trace)) {
    printf("caller %p free %p\n", __GET_CALLER(), ptr);
  }

  cmpct_free(ptr);
}

void sized_free(void* ptr, size_t s) {
  DEBUG_ASSERT(!arch_blocking_disallowed());

  LTRACEF("ptr %p size %lu\n", ptr, s);
  if (unlikely(heap_trace)) {
    printf("caller %p free %p size %lu\n", __GET_CALLER(), ptr, s);
  }

  cmpct_sized_free(ptr, s);
}

static void heap_dump(bool panic_time) { cmpct_dump(panic_time); }

void heap_get_info(size_t* total_bytes, size_t* free_bytes) {
  size_t used_bytes;
  size_t cached_bytes;
  cmpct_get_info(&used_bytes, free_bytes, &cached_bytes);
  if (total_bytes) {
    *total_bytes = used_bytes + cached_bytes;
  }
}

static void heap_test() { cmpct_test(); }

void* heap_page_alloc(size_t pages) {
  DEBUG_ASSERT(pages > 0);

  if constexpr (VIRTUAL_HEAP) {
    zx::result<vaddr_t> result = virtual_alloc->AllocPages(pages);
    if (result.is_error()) {
      printf("Failed to allocate %zu pages for heap: %d\n", pages, result.error_value());
      return nullptr;
    }

#if __has_feature(address_sanitizer)
    asan_poison_shadow(*result, pages * PAGE_SIZE, kAsanInternalHeapMagic);
#endif  // __has_feature(address_sanitizer)

    void* ret = reinterpret_cast<void*>(*result);
    LTRACEF("pages %zu: va %p\n", pages, ret);
    return ret;
  } else {
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
      p->set_state(vm_page_state::HEAP);
#if __has_feature(address_sanitizer)
      void* const vaddr = paddr_to_physmap(p->paddr());
      asan_poison_shadow(reinterpret_cast<uintptr_t>(vaddr), PAGE_SIZE, kAsanInternalHeapMagic);
#endif  // __has_feature(address_sanitizer)
    }

    LTRACEF("pages %zu: pa %#lx, va %p\n", pages, pa, paddr_to_physmap(pa));

    return paddr_to_physmap(pa);
  }
}

void heap_page_free(void* _ptr, size_t pages) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED((uintptr_t)_ptr));
  DEBUG_ASSERT(pages > 0);

  LTRACEF("ptr %p, pages %zu\n", _ptr, pages);

  if constexpr (VIRTUAL_HEAP) {
    vaddr_t ptr = reinterpret_cast<vaddr_t>(_ptr);
    virtual_alloc->FreePages(ptr, pages);
  } else {
    uint8_t* ptr = (uint8_t*)_ptr;

    list_node list;
    list_initialize(&list);

    while (pages > 0) {
      vm_page_t* p = paddr_to_vm_page(vaddr_to_paddr(ptr));
      if (p) {
        DEBUG_ASSERT(p->state() == vm_page_state::HEAP);
        DEBUG_ASSERT(!list_in_list(&p->queue_node));

        list_add_tail(&list, &p->queue_node);
      }

      ptr += PAGE_SIZE;
      pages--;
    }

    pmm_free(&list);
  }
}

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
  } else {
    printf("unrecognized command\n");
    goto usage;
  }

  return 0;
}
