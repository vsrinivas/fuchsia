// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "pmm_arena.h"

#include <err.h>
#include <inttypes.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <ktl/limits.h>
#include <pretty/sizes.h>
#include <vm/bootalloc.h>
#include <vm/bootreserve.h>
#include <vm/physmap.h>

#include "pmm_node.h"
#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

zx_status_t PmmArena::Init(const pmm_arena_info_t* info, PmmNode* node) {
  // TODO: validate that info is sane (page aligned, etc)
  info_ = *info;

  // allocate an array of pages to back this one
  size_t page_count = size() / PAGE_SIZE;
  size_t page_array_size = ROUNDUP_PAGE_SIZE(page_count * sizeof(vm_page));

  // if the arena is too small to be useful, bail
  if (page_array_size >= size()) {
    printf("PMM: arena too small to be useful (size %zu)\n", size());
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // allocate a chunk to back the page array out of the arena itself, near the top of memory
  reserve_range_t range;
  auto status = boot_reserve_range_search(base(), size(), page_array_size, &range);
  if (status != ZX_OK) {
    printf("PMM: arena intersects with reserved memory in unresovable way\n");
    return ZX_ERR_NO_MEMORY;
  }

  DEBUG_ASSERT(range.pa >= base() && range.len <= page_array_size);

  // get the kernel pointer
  void* raw_page_array = paddr_to_physmap(range.pa);
  LTRACEF("arena for base 0%#" PRIxPTR " size %#zx page array at %p size %#zx\n", base(), size(),
          raw_page_array, page_array_size);

  memset(raw_page_array, 0, page_array_size);

  page_array_ = (vm_page_t*)raw_page_array;

  // we've just constructed |page_count| pages in the state VM_PAGE_STATE_FREE
  vm_page::add_to_initial_count(VM_PAGE_STATE_FREE, page_count);

  // compute the range of the array that backs the array itself
  size_t array_start_index = (PAGE_ALIGN(range.pa) - info_.base) / PAGE_SIZE;
  size_t array_end_index = array_start_index + page_array_size / PAGE_SIZE;
  LTRACEF("array_start_index %zu, array_end_index %zu, page_count %zu\n", array_start_index,
          array_end_index, page_count);

  DEBUG_ASSERT(array_start_index < page_count && array_end_index <= page_count);

  // add all pages that aren't part of the page array to the free list
  // pages part of the free array go to the WIRED state
  list_node list;
  list_initialize(&list);
  for (size_t i = 0; i < page_count; i++) {
    auto& p = page_array_[i];

    p.paddr_priv = base() + i * PAGE_SIZE;
    if (i >= array_start_index && i < array_end_index) {
      p.set_state(VM_PAGE_STATE_WIRED);
    } else {
      list_add_tail(&list, &p.queue_node);
    }
  }

  node->AddFreePages(&list);

  return ZX_OK;
}

vm_page_t* PmmArena::FindSpecific(paddr_t pa) {
  if (!address_in_arena(pa)) {
    return nullptr;
  }

  size_t index = (pa - base()) / PAGE_SIZE;

  DEBUG_ASSERT(index < size() / PAGE_SIZE);

  return get_page(index);
}

vm_page_t* PmmArena::FindFreeContiguous(size_t count, uint8_t alignment_log2) {
  // walk the list starting at alignment boundaries.
  // calculate the starting offset into this arena, based on the
  // base address of the arena to handle the case where the arena
  // is not aligned on the same boundary requested.
  paddr_t rounded_base = ROUNDUP(base(), 1UL << alignment_log2);
  if (rounded_base < base() || rounded_base > base() + size() - 1) {
    return 0;
  }

  paddr_t aligned_offset = (rounded_base - base()) / PAGE_SIZE;
  paddr_t start = aligned_offset;
  LTRACEF("starting search at aligned offset %#" PRIxPTR "\n", start);
  LTRACEF("arena base %#" PRIxPTR " size %zu\n", base(), size());

retry:
  // search while we're still within the arena and have a chance of finding a slot
  // (start + count < end of arena)
  while ((start < size() / PAGE_SIZE) && ((start + count) <= size() / PAGE_SIZE)) {
    vm_page_t* p = &page_array_[start];
    for (uint i = 0; i < count; i++) {
      if (!p->is_free()) {
        // this run is broken, break out of the inner loop.
        // start over at the next alignment boundary
        start = ROUNDUP(start - aligned_offset + i + 1, 1UL << (alignment_log2 - PAGE_SIZE_SHIFT)) +
                aligned_offset;
        goto retry;
      }
      p++;
    }

    // we found a run
    p = &page_array_[start];
    LTRACEF("found run from pa %#" PRIxPTR " to %#" PRIxPTR "\n", p->paddr(),
            p->paddr() + count * PAGE_SIZE);

    return p;
  }

  return nullptr;
}

void PmmArena::CountStates(size_t state_count[VM_PAGE_STATE_COUNT_]) const {
  for (size_t i = 0; i < size() / PAGE_SIZE; i++) {
    state_count[page_array_[i].state()]++;
  }
}

void PmmArena::Dump(bool dump_pages, bool dump_free_ranges) const {
  char pbuf[16];
  printf("  arena %p: name '%s' base %#" PRIxPTR " size %s (0x%zx) flags 0x%x\n", this,
         name(), base(), format_size(pbuf, sizeof(pbuf), size()), size(), flags());
  printf("\tpage_array %p\n", page_array_);

  // dump all of the pages
  if (dump_pages) {
    for (size_t i = 0; i < size() / PAGE_SIZE; i++) {
      page_array_[i].dump();
    }
  }

  // count the number of pages in every state
  size_t state_count[VM_PAGE_STATE_COUNT_] = {};
  CountStates(state_count);

  printf("\tpage states:\n");
  for (unsigned int i = 0; i < VM_PAGE_STATE_COUNT_; i++) {
    printf("\t\t%-12s %-16zu (%zu bytes)\n", page_state_to_string(i), state_count[i],
           state_count[i] * PAGE_SIZE);
  }

  // dump the free pages
  if (dump_free_ranges) {
    printf("\tfree ranges:\n");
    ssize_t last = -1;
    for (size_t i = 0; i < size() / PAGE_SIZE; i++) {
      if (page_array_[i].is_free()) {
        if (last == -1) {
          last = i;
        }
      } else {
        if (last != -1) {
          printf("\t\t%#" PRIxPTR " - %#" PRIxPTR "\n", base() + last * PAGE_SIZE,
                 base() + i * PAGE_SIZE);
        }
        last = -1;
      }
    }

    if (last != -1) {
      printf("\t\t%#" PRIxPTR " - %#" PRIxPTR "\n", base() + last * PAGE_SIZE, base() + size());
    }
  }
}
