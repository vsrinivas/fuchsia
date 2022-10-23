// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_VM_PMM_ARENA_H_
#define ZIRCON_KERNEL_VM_PMM_ARENA_H_

#include <lib/zx/result.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <vm/pmm.h>

class PmmNode;

class PmmArena : public fbl::DoublyLinkedListable<PmmArena*> {
 public:
  constexpr PmmArena() = default;
  ~PmmArena() = default;

  DISALLOW_COPY_ASSIGN_AND_MOVE(PmmArena);

  // initialize the arena and allocate memory for internal data structures
  zx_status_t Init(const pmm_arena_info_t* info, PmmNode* node);

  zx_status_t InitForTest(const pmm_arena_info_t& info, vm_page_t* page_array);

  // accessors
  const pmm_arena_info_t& info() const { return info_; }
  const char* name() const { return info_.name; }
  paddr_t base() const { return info_.base; }
  size_t size() const { return info_.size; }
  unsigned int flags() const { return info_.flags; }

  // Counts the number of pages in every state. For each page in the arena,
  // increments the corresponding vm_page_state::*-indexed entry of
  // |state_count|. Does not zero out the entries first.
  void CountStates(size_t state_count[VmPageStateIndex(vm_page_state::COUNT_)]) const;

  vm_page_t* get_page(size_t index) { return &page_array_[index]; }

  // find a free run of contiguous pages
  vm_page_t* FindFreeContiguous(size_t count, uint8_t alignment_log2);

  // return a pointer to a specific page
  vm_page_t* FindSpecific(paddr_t pa);

  // helpers
  bool page_belongs_to_arena(const vm_page* page) const {
    return (page->paddr() >= base() && page->paddr() < (base() + size()));
  }

  bool address_in_arena(paddr_t address) const {
    return (address >= info_.base && address <= info_.base + info_.size - 1);
  }

  void Dump(bool dump_pages, bool dump_free_ranges) const;

 private:
  // Walks the region defined by |offset| and |count| and returns the index of
  // the last non-free page or ZX_ERR_NOT_FOUND if all pages are free.
  //
  // It is an error if the range specified by |offset| and |count| is not
  // completely contained within the arena.
  //
  // A loaned page is considered non-free for purposes of contiguous memory
  // allocation.
  zx::result<uint64_t> FindLastNonFree(uint64_t offset, size_t count) const;

  pmm_arena_info_t info_ = {};
  vm_page_t* page_array_ = nullptr;
  // The index into |page_array_| at which the next |FindFreeContiguous| serach
  // should begin.  Used to optimize |FindFreeContiguous|.
  uint64_t search_hint_ = 0;
};

#endif  // ZIRCON_KERNEL_VM_PMM_ARENA_H_
