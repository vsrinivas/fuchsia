// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <lib/arch/intrin.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/fit/defer.h>
#include <trace.h>

#include <arch/x86/feature.h>
#include <arch/x86/page_tables/constants.h>
#include <arch/x86/page_tables/page_tables.h>
#include <fbl/algorithm.h>
#include <ktl/algorithm.h>
#include <ktl/iterator.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

namespace {

// Return the page size for this level
size_t page_size(PageTableLevel level) {
  switch (level) {
    case PageTableLevel::PT_L:
      return 1ULL << PT_SHIFT;
    case PageTableLevel::PD_L:
      return 1ULL << PD_SHIFT;
    case PageTableLevel::PDP_L:
      return 1ULL << PDP_SHIFT;
    case PageTableLevel::PML4_L:
      return 1ULL << PML4_SHIFT;
    default:
      panic("page_size: invalid level\n");
  }
}

// Whether an address is aligned to the page size of this level
bool page_aligned(PageTableLevel level, vaddr_t vaddr) {
  return (vaddr & (page_size(level) - 1)) == 0;
}

// Extract the index needed for finding |vaddr| for the given level
uint vaddr_to_index(PageTableLevel level, vaddr_t vaddr) {
  switch (level) {
    case PageTableLevel::PML4_L:
      return VADDR_TO_PML4_INDEX(vaddr);
    case PageTableLevel::PDP_L:
      return VADDR_TO_PDP_INDEX(vaddr);
    case PageTableLevel::PD_L:
      return VADDR_TO_PD_INDEX(vaddr);
    case PageTableLevel::PT_L:
      return VADDR_TO_PT_INDEX(vaddr);
    default:
      panic("vaddr_to_index: invalid level\n");
  }
}

// Convert a PTE to a physical address
paddr_t paddr_from_pte(PageTableLevel level, pt_entry_t pte) {
  DEBUG_ASSERT(IS_PAGE_PRESENT(pte));

  paddr_t pa;
  switch (level) {
    case PageTableLevel::PDP_L:
      pa = (pte & X86_HUGE_PAGE_FRAME);
      break;
    case PageTableLevel::PD_L:
      pa = (pte & X86_LARGE_PAGE_FRAME);
      break;
    case PageTableLevel::PT_L:
      pa = (pte & X86_PG_FRAME);
      break;
    default:
      panic("paddr_from_pte at unhandled level %d\n", static_cast<int>(level));
  }

  return pa;
}

PageTableLevel lower_level(PageTableLevel level) {
  DEBUG_ASSERT(level != PageTableLevel::PT_L);
  return static_cast<PageTableLevel>(static_cast<int>(level) - 1);
}

bool page_table_is_clear(const volatile pt_entry_t* page_table) {
  uint lower_idx;
  for (lower_idx = 0; lower_idx < NO_OF_PT_ENTRIES; ++lower_idx) {
    if (IS_PAGE_PRESENT(page_table[lower_idx])) {
      return false;
    }
  }
  return true;
}

}  // namespace

void PendingTlbInvalidation::enqueue(vaddr_t v, PageTableLevel level, bool is_global_page,
                                     bool is_terminal) {
  if (is_global_page) {
    contains_global = true;
  }

  // We mark PML4_L entries as full shootdowns, since it's going to be
  // expensive one way or another.
  if (count >= ktl::size(item) || level == PageTableLevel::PML4_L) {
    full_shootdown = true;
    return;
  }
  item[count].set_page_level(static_cast<uint64_t>(level));
  item[count].set_is_global(is_global_page);
  item[count].set_is_terminal(is_terminal);
  item[count].set_encoded_addr(v >> PAGE_SIZE_SHIFT);
  count++;
}

void PendingTlbInvalidation::clear() {
  count = 0;
  full_shootdown = false;
  contains_global = false;
}

PendingTlbInvalidation::~PendingTlbInvalidation() { DEBUG_ASSERT(count == 0); }

// Utility for coalescing cache line flushes when modifying page tables.  This
// allows us to mutate adjacent page table entries without having to flush for
// each cache line multiple times.
class CacheLineFlusher {
 public:
  // If |perform_invalidations| is false, this class acts as a no-op.
  explicit CacheLineFlusher(bool perform_invalidations);
  ~CacheLineFlusher();
  void FlushPtEntry(const volatile pt_entry_t* entry);

  void ForceFlush();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(CacheLineFlusher);

  // The cache-aligned address that currently dirty.  If 0, no dirty line.
  uintptr_t dirty_line_;

  const uintptr_t cl_mask_;
  const bool perform_invalidations_;
};

CacheLineFlusher::CacheLineFlusher(bool perform_invalidations)
    : dirty_line_(0),
      cl_mask_(~(arch::BootCpuid<arch::CpuidProcessorInfo>().cache_line_size_bytes() - 1ull)),
      perform_invalidations_(perform_invalidations) {}

CacheLineFlusher::~CacheLineFlusher() { ForceFlush(); }

void CacheLineFlusher::ForceFlush() {
  if (dirty_line_ && perform_invalidations_) {
    __asm__ volatile("clflush %0\n" : : "m"(*reinterpret_cast<char*>(dirty_line_)) : "memory");
    dirty_line_ = 0;
  }
}

void CacheLineFlusher::FlushPtEntry(const volatile pt_entry_t* entry) {
  uintptr_t entry_line = reinterpret_cast<uintptr_t>(entry) & cl_mask_;
  if (entry_line != dirty_line_) {
    ForceFlush();
    dirty_line_ = entry_line;
  }
}

// Utility for managing consistency of the page tables from a cache and TLB
// point-of-view.  It ensures that memory is not freed while a TLB entry may
// refer to it, and that changes to the page tables have appropriate visiblity
// to the hardware interpreting them.  Finish MUST be called on this
// class, even if the page table change failed.
class X86PageTableBase::ConsistencyManager {
 public:
  explicit ConsistencyManager(X86PageTableBase* pt);
  ~ConsistencyManager();

  void queue_free(vm_page_t* page) {
    AssertHeld(pt_->lock_);
    DEBUG_ASSERT(page->state() == vm_page_state::MMU);
    list_add_tail(&to_free_, &page->queue_node);
    DEBUG_ASSERT(pt_->pages_ > 0);
    pt_->pages_--;
  }

  CacheLineFlusher* cache_line_flusher() { return &clf_; }
  PendingTlbInvalidation* pending_tlb() { return &tlb_; }

  // This function must be called while holding pt_->lock_.
  void Finish();

  void SetFullShootdown() { tlb_.full_shootdown = true; }

 private:
  X86PageTableBase* pt_;

  // Cache line to flush prior to TLB invalidations
  CacheLineFlusher clf_;

  // TLB invalidations that need to occur
  PendingTlbInvalidation tlb_;

  // vm_page_t's to relese to the PMM after the TLB invalidation occurs
  list_node to_free_ = LIST_INITIAL_VALUE(to_free_);
};

X86PageTableBase::ConsistencyManager::ConsistencyManager(X86PageTableBase* pt)
    : pt_(pt), clf_(pt->needs_cache_flushes()) {}

X86PageTableBase::ConsistencyManager::~ConsistencyManager() {
  DEBUG_ASSERT(pt_ == nullptr);

  // We free the paging structures here rather than in Finish(), to allow
  // support deferring invoking pmm_free() until after we've left the page
  // table lock.
  vm_page_t* p;
  list_for_every_entry (&to_free_, p, vm_page_t, queue_node) {
    DEBUG_ASSERT(p->state() == vm_page_state::MMU);
  }
  if (!list_is_empty(&to_free_)) {
    pmm_free(&to_free_);
  }
}

void X86PageTableBase::ConsistencyManager::Finish() {
  DEBUG_ASSERT(pt_->lock_.lock().IsHeld());

  clf_.ForceFlush();
  if (pt_->needs_cache_flushes()) {
    // If the hardware needs cache flushes for the tables to be visible,
    // make sure we serialize the flushes before issuing the TLB
    // invalidations.
    arch::DeviceMemoryBarrier();
  }
  pt_->TlbInvalidate(&tlb_);
  pt_ = nullptr;
}

class MappingCursor {
 public:
  MappingCursor() = default;
  MappingCursor(vaddr_t vaddr, size_t size) : vaddr_(vaddr), size_(size) {}
  MappingCursor(const paddr_t* paddrs, size_t paddr_count, size_t page_size, vaddr_t vaddr,
                size_t size)
      : paddrs_(paddrs), page_size_(page_size), vaddr_(vaddr), size_(size) {
#ifdef DEBUG_ASSERT_IMPLEMENTED
    paddr_count_ = paddr_count;
#endif
  }
  /**
   * @brief Update the cursor to skip over a not-present page table entry.
   */
  void SkipEntry(PageTableLevel level) {
    const size_t ps = page_size(level);
    // Calculate the amount the cursor should skip to get to the next entry at
    // this page table level.
    const size_t next_entry_offset = ps - (vaddr_ & (ps - 1));
    // If our endpoint was in the middle of this range, clamp the
    // amount we remove from the cursor
    const size_t consume = (size_ > next_entry_offset) ? next_entry_offset : size_;

    DEBUG_ASSERT(size_ >= consume);
    size_ -= consume;
    vaddr_ += consume;

    // Skipping entries has no meaning if we are attempting to create new mappings as we cannot
    // manipulate the paddr in a sensible way, so we expect this to not get called.
    DEBUG_ASSERT(!paddrs_);
    DEBUG_ASSERT(page_size_ == 0);
  }

  void ConsumePAddr(size_t ps) {
    DEBUG_ASSERT(paddrs_);
    paddr_consumed_ += ps;
    DEBUG_ASSERT(paddr_consumed_ <= page_size_);
    vaddr_ += ps;
    DEBUG_ASSERT(size_ >= ps);
    size_ -= ps;
    if (paddr_consumed_ == page_size_) {
      paddrs_++;
      paddr_consumed_ = 0;
#ifdef DEBUG_ASSERT_IMPLEMENTED
      DEBUG_ASSERT(paddr_count_ > 0);
      paddr_count_--;
#endif
    }
  }

  void ConsumeVAddr(size_t ps) {
    // If physical addresses are being tracked for creating mappings then ConsumePAddr should be
    // called, so validate there are no paddrs we are going to desync with.
    DEBUG_ASSERT(!paddrs_);
    DEBUG_ASSERT(page_size_ == 0);
    vaddr_ += ps;
    DEBUG_ASSERT(size_ >= ps);
    size_ -= ps;
  }

  // Provides a way to transition a mapping cursor from one that tracks paddrs to one that just
  // tracks the remaining virtual range. This is useful when a cursor was being used to track
  // mapping pages but then needs to be used to just track the virtual range to unmap / rollback.
  void DropPAddrs() {
    paddrs_ = nullptr;
    page_size_ = 0;
    paddr_consumed_ = 0;
  }

  paddr_t paddr() const {
    DEBUG_ASSERT(paddrs_);
    DEBUG_ASSERT(size_ > 0);
    return (*paddrs_) + paddr_consumed_;
  }

  size_t PageRemaining() const {
    DEBUG_ASSERT(paddrs_);
    return page_size_ - paddr_consumed_;
  }

  vaddr_t vaddr() const { return vaddr_; }

  size_t size() const { return size_; }

 private:
  // Physical address is optional and only applies when mapping in new pages, and not manipulating
  // existing mappings.
  const paddr_t* paddrs_ = nullptr;
#ifdef DEBUG_ASSERT_IMPLEMENTED
  // We have no need to actually track the total number of elements in the paddrs array, as this
  // should be a simple size/paddr_size. To guard against code mistakes though, we separately track
  // this just in debug mode.
  size_t paddr_count_ = 0;
#endif
  size_t paddr_consumed_ = 0;
  size_t page_size_ = 0;

  vaddr_t vaddr_;
  size_t size_;
};

void X86PageTableBase::UpdateEntry(ConsistencyManager* cm, PageTableLevel level, vaddr_t vaddr,
                                   volatile pt_entry_t* pte, paddr_t paddr, PtFlags flags,
                                   bool was_terminal, bool exact_flags) {
  DEBUG_ASSERT(pte);
  DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

  pt_entry_t olde = *pte;
  pt_entry_t newe = paddr | flags | X86_MMU_PG_P;

  // Check if we are actually changing anything, ignoring the accessed and dirty bits unless
  // exact_flags has been requested to allow for those bits to be explicitly unset.
  if ((olde & ~(exact_flags ? 0 : (X86_MMU_PG_A | X86_MMU_PG_D))) == newe) {
    return;
  }

  /* set the new entry */
  *pte = newe;
  cm->cache_line_flusher()->FlushPtEntry(pte);

  /* attempt to invalidate the page */
  if (IS_PAGE_PRESENT(olde)) {
    cm->pending_tlb()->enqueue(vaddr, level, /*is_global_page=*/olde & X86_MMU_PG_G, was_terminal);
  }
}

void X86PageTableBase::UnmapEntry(ConsistencyManager* cm, PageTableLevel level, vaddr_t vaddr,
                                  volatile pt_entry_t* pte, bool was_terminal) {
  DEBUG_ASSERT(pte);

  pt_entry_t olde = *pte;

  *pte = 0;
  cm->cache_line_flusher()->FlushPtEntry(pte);

  /* attempt to invalidate the page */
  if (IS_PAGE_PRESENT(olde)) {
    cm->pending_tlb()->enqueue(vaddr, level, /*is_global_page=*/olde & X86_MMU_PG_G, was_terminal);
  }
}

/**
 * @brief Allocating a new page table
 */
pt_entry_t* X86PageTableBase::AllocatePageTable() {
  paddr_t pa;
  vm_page* p;
  zx_status_t status;

  // The default allocation routine is pmm_alloc_page so test and explicitly call it
  // to avoid any unnecessary virtual function calls.
  if (likely(!test_page_alloc_func_)) {
    status = pmm_alloc_page(0, &p, &pa);
  } else {
    status = test_page_alloc_func_(0, &p, &pa);
  }
  if (status != ZX_OK) {
    return nullptr;
  }
  p->set_state(vm_page_state::MMU);

  pt_entry_t* page_ptr = static_cast<pt_entry_t*>(paddr_to_physmap(pa));
  DEBUG_ASSERT(page_ptr);

  arch_zero_page(page_ptr);

  return page_ptr;
}

/*
 * @brief Split the given large page into smaller pages
 */
zx_status_t X86PageTableBase::SplitLargePage(PageTableLevel level, vaddr_t vaddr,
                                             volatile pt_entry_t* pte, ConsistencyManager* cm) {
  DEBUG_ASSERT_MSG(level != PageTableLevel::PT_L, "tried splitting PT_L");
  LTRACEF_LEVEL(2, "splitting table %p at level %d\n", pte, static_cast<int>(level));

  DEBUG_ASSERT(IS_PAGE_PRESENT(*pte) && IS_LARGE_PAGE(*pte));
  volatile pt_entry_t* m = AllocatePageTable();
  if (m == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  paddr_t paddr_base = paddr_from_pte(level, *pte);
  PtFlags flags = split_flags(level, *pte & X86_LARGE_FLAGS_MASK);

  DEBUG_ASSERT(page_aligned(level, vaddr));
  vaddr_t new_vaddr = vaddr;
  paddr_t new_paddr = paddr_base;
  size_t ps = page_size(lower_level(level));
  for (int i = 0; i < NO_OF_PT_ENTRIES; i++) {
    volatile pt_entry_t* e = m + i;
    // If this is a PDP_L (i.e. huge page), flags will include the
    // PS bit still, so the new PD entries will be large pages.
    UpdateEntry(cm, lower_level(level), new_vaddr, e, new_paddr, flags, /*was_terminal=*/false);
    new_vaddr += ps;
    new_paddr += ps;
  }
  DEBUG_ASSERT(new_vaddr == vaddr + page_size(level));

  flags = intermediate_flags();
  UpdateEntry(cm, level, vaddr, pte, X86_VIRT_TO_PHYS(m), flags, /*was_terminal=*/true);
  pages_++;
  return ZX_OK;
}

/*
 * @brief given a page table entry, return a pointer to the next page table one level down
 */
static inline volatile pt_entry_t* get_next_table_from_entry(pt_entry_t entry) {
  if (!IS_PAGE_PRESENT(entry) || IS_LARGE_PAGE(entry))
    return nullptr;

  return reinterpret_cast<volatile pt_entry_t*>(X86_PHYS_TO_VIRT(entry & X86_PG_FRAME));
}

/**
 * @brief  Walk the page table structures returning the entry and level that maps the address.
 *
 * @param table The top-level paging structure's virtual address
 * @param vaddr The virtual address to retrieve the mapping for
 * @param ret_level The level of the table that defines the found mapping
 * @param mapping The mapping that was found
 *
 * @return ZX_OK if mapping is found
 * @return ZX_ERR_NOT_FOUND if mapping is not found
 */
zx_status_t X86PageTableBase::GetMapping(volatile pt_entry_t* table, vaddr_t vaddr,
                                         PageTableLevel level, PageTableLevel* ret_level,
                                         volatile pt_entry_t** mapping) {
  DEBUG_ASSERT(table);
  DEBUG_ASSERT(ret_level);
  DEBUG_ASSERT(mapping);

  if (level == PageTableLevel::PT_L) {
    return GetMappingL0(table, vaddr, ret_level, mapping);
  }

  LTRACEF_LEVEL(2, "table %p\n", table);

  uint index = vaddr_to_index(level, vaddr);
  volatile pt_entry_t* e = table + index;
  DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(e))->state() != vm_page_state::FREE);
  pt_entry_t pt_val = *e;
  if (!IS_PAGE_PRESENT(pt_val))
    return ZX_ERR_NOT_FOUND;

  /* if this is a large page, stop here */
  if (IS_LARGE_PAGE(pt_val)) {
    *mapping = e;
    *ret_level = level;
    return ZX_OK;
  }

  volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
  // TODO(fxbug.dev/66978): If you find that the following assert is firing and you're running QEMU
  // TCG (i.e. no KVM), then you may be observing a known bug in upstream QEMU.  See fxbug.dev/66978
  // for details.
  DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(next_table))->state() != vm_page_state::FREE);
  return GetMapping(next_table, vaddr, lower_level(level), ret_level, mapping);
}

zx_status_t X86PageTableBase::GetMappingL0(volatile pt_entry_t* table, vaddr_t vaddr,
                                           PageTableLevel* ret_level,
                                           volatile pt_entry_t** mapping) {
  /* do the final page table lookup */
  uint index = vaddr_to_index(PageTableLevel::PT_L, vaddr);
  volatile pt_entry_t* e = table + index;
  DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(e))->state() != vm_page_state::FREE);
  if (!IS_PAGE_PRESENT(*e))
    return ZX_ERR_NOT_FOUND;

  *mapping = e;
  *ret_level = PageTableLevel::PT_L;
  return ZX_OK;
}

/**
 * @brief Unmaps the range specified by start_cursor.
 *
 * Level must be top_level() when invoked.  The caller must, even on failure,
 * free all pages in the |to_free| list and adjust the |pages_| count.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * unmap within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return true if the caller (i.e. the next level up page table) might need to
 * free this page table.
 */
zx::result<bool> X86PageTableBase::RemoveMapping(volatile pt_entry_t* table, PageTableLevel level,
                                                 EnlargeOperation enlarge,
                                                 const MappingCursor& start_cursor,
                                                 MappingCursor* new_cursor,
                                                 ConsistencyManager* cm) {
  DEBUG_ASSERT(table);
  LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", static_cast<int>(level), start_cursor.vaddr(),
          start_cursor.size());
  DEBUG_ASSERT(check_vaddr(start_cursor.vaddr()));

  if (level == PageTableLevel::PT_L) {
    return zx::ok(RemoveMappingL0(table, start_cursor, new_cursor, cm));
  }

  *new_cursor = start_cursor;

  bool unmapped = false;
  // Track if there are any entries at all. This is necessary to properly rollback if an
  // attempt to map a page fails to allocate a page table, as that case can result in an
  // empty non-last-level page table.
  bool any_pages = false;
  size_t ps = page_size(level);
  uint index = vaddr_to_index(level, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(e))->state() != vm_page_state::FREE);
    pt_entry_t pt_val = *e;
    // If the page isn't even mapped, just skip it
    if (!IS_PAGE_PRESENT(pt_val)) {
      new_cursor->SkipEntry(level);
      DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
      continue;
    }
    any_pages = true;

    if (IS_LARGE_PAGE(pt_val)) {
      bool vaddr_level_aligned = page_aligned(level, new_cursor->vaddr());
      // If the request covers the entire large page, just unmap it
      if (vaddr_level_aligned && new_cursor->size() >= ps) {
        UnmapEntry(cm, level, new_cursor->vaddr(), e, /*was_terminal=*/true);
        unmapped = true;

        new_cursor->ConsumeVAddr(ps);
        DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
        continue;
      }
      // Otherwise, we need to split it
      vaddr_t page_vaddr = new_cursor->vaddr() & ~(ps - 1);
      zx_status_t status = SplitLargePage(level, page_vaddr, e, cm);
      if (status != ZX_OK) {
        // If split fails, just unmap the whole thing, and let a
        // subsequent page fault clean it up.
        if (enlarge == EnlargeOperation::Yes) {
          UnmapEntry(cm, level, new_cursor->vaddr(), e, /*was_terminal=*/true);
          unmapped = true;

          new_cursor->SkipEntry(level);
          DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
          continue;
        } else {
          return zx::error(status);
        }
      }
      pt_val = *e;
    }

    MappingCursor cursor;
    volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
    auto status = RemoveMapping(next_table, lower_level(level), enlarge, *new_cursor, &cursor, cm);
    if (status.is_error()) {
      return status;
    }
    bool lower_unmapped = status.value();

    // If we were requesting to unmap everything in the lower page table,
    // we know we can unmap the lower level page table.  Otherwise, if
    // we unmapped anything in the lower level, check to see if that
    // level is now empty.
    bool unmap_page_table = page_aligned(level, new_cursor->vaddr()) && new_cursor->size() >= ps;
    if (!unmap_page_table && lower_unmapped) {
      unmap_page_table = page_table_is_clear(next_table);
    }
    if (unmap_page_table) {
      paddr_t ptable_phys = X86_VIRT_TO_PHYS(next_table);
      LTRACEF("L: %d free pt v %#" PRIxPTR " phys %#" PRIxPTR "\n", static_cast<int>(level),
              (uintptr_t)next_table, ptable_phys);

      vm_page_t* page = paddr_to_vm_page(ptable_phys);
      DEBUG_ASSERT(page == paddr_to_vm_page(X86_VIRT_TO_PHYS(get_next_table_from_entry(*e))));
      UnmapEntry(cm, level, new_cursor->vaddr(), e, /*was_terminal=*/false);

      DEBUG_ASSERT(page);
      DEBUG_ASSERT_MSG(page->state() == vm_page_state::MMU,
                       "page %p state %u, paddr %#" PRIxPTR "\n", page,
                       static_cast<uint32_t>(page->state()), X86_VIRT_TO_PHYS(next_table));
      DEBUG_ASSERT(!list_in_list(&page->queue_node));

      cm->queue_free(page);
      unmapped = true;
    }
    *new_cursor = cursor;
    DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());

    DEBUG_ASSERT(new_cursor->size() == 0 || page_aligned(level, new_cursor->vaddr()));
  }

  return zx::ok(unmapped || !any_pages);
}

// Base case of RemoveMapping for smallest page size.
bool X86PageTableBase::RemoveMappingL0(volatile pt_entry_t* table,
                                       const MappingCursor& start_cursor, MappingCursor* new_cursor,
                                       ConsistencyManager* cm) {
  LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr(), start_cursor.size());
  DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size()));

  *new_cursor = start_cursor;

  bool unmapped = false;
  uint index = vaddr_to_index(PageTableLevel::PT_L, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(e))->state() != vm_page_state::FREE);
    if (IS_PAGE_PRESENT(*e)) {
      UnmapEntry(cm, PageTableLevel::PT_L, new_cursor->vaddr(), e, /*was_terminal=*/true);
      unmapped = true;
    }

    new_cursor->ConsumeVAddr(PAGE_SIZE);
    DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
  }
  return unmapped;
}

/**
 * @brief Creates mappings for the range specified by start_cursor
 *
 * Level must be top_level() when invoked.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return ZX_OK if successful
 * @return ZX_ERR_ALREADY_EXISTS if the range overlaps an existing mapping and existing_action is
 * set to Error
 * @return ZX_ERR_NO_MEMORY if intermediate page tables could not be allocated
 */
zx_status_t X86PageTableBase::AddMapping(volatile pt_entry_t* table, uint mmu_flags,
                                         PageTableLevel level, ExistingEntryAction existing_action,
                                         const MappingCursor& start_cursor,
                                         MappingCursor* new_cursor, ConsistencyManager* cm) {
  DEBUG_ASSERT(table);
  DEBUG_ASSERT(check_vaddr(start_cursor.vaddr()));
  DEBUG_ASSERT(check_paddr(start_cursor.paddr()));

  zx_status_t ret = ZX_OK;
  *new_cursor = start_cursor;

  if (level == PageTableLevel::PT_L) {
    return AddMappingL0(table, mmu_flags, existing_action, start_cursor, new_cursor, cm);
  }

  auto abort = fit::defer([&]() {
    AssertHeld(lock_);
    if (level == top_level()) {
      // Build an unmap cursor. new_cursor->size should be how much is left to be mapped still.
      MappingCursor cursor(/*vaddr=*/start_cursor.vaddr(),
                           /*size=*/start_cursor.size() - new_cursor->size());
      MappingCursor result;
      if (cursor.size() > 0) {
        auto status = RemoveMapping(table, level, EnlargeOperation::No, cursor, &result, cm);
        // Removing the exact mappings we just added should never be able to fail.
        ASSERT(status.is_ok());
        DEBUG_ASSERT(result.size() == 0);
      }
    }
  });

  IntermediatePtFlags interm_flags = intermediate_flags();
  PtFlags term_flags = terminal_flags(level, mmu_flags);

  size_t ps = page_size(level);
  bool level_supports_large_pages = supports_page_size(level);
  uint index = vaddr_to_index(level, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(e))->state() != vm_page_state::FREE);
    pt_entry_t pt_val = *e;

    // See if there's a large page in our way
    if (IS_PAGE_PRESENT(pt_val) && IS_LARGE_PAGE(pt_val)) {
      if (existing_action == ExistingEntryAction::Error) {
        return ZX_ERR_ALREADY_EXISTS;
      }
      new_cursor->ConsumePAddr(ps);
      DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
      continue;
    }

    // Check if this is a candidate for a new large page
    bool level_valigned = page_aligned(level, new_cursor->vaddr());
    bool level_paligned = page_aligned(level, new_cursor->paddr());
    if (level_supports_large_pages && !IS_PAGE_PRESENT(pt_val) && level_valigned &&
        level_paligned && new_cursor->PageRemaining() >= ps) {
      UpdateEntry(cm, level, new_cursor->vaddr(), table + index, new_cursor->paddr(),
                  term_flags | X86_MMU_PG_PS, /*was_terminal=*/false);
      new_cursor->ConsumePAddr(ps);
      DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
    } else {
      // See if we need to create a new table
      if (!IS_PAGE_PRESENT(pt_val)) {
        volatile pt_entry_t* m = AllocatePageTable();
        if (m == nullptr) {
          // The mapping wasn't fully updated, but there is work here
          // that might need to be undone.
          size_t partial_update = ktl::min(ps, new_cursor->size());
          // Cancel paddr tracking so we account for the virtual range we need to
          // unmap without needing to increment in page appropriate amounts.
          new_cursor->DropPAddrs();
          new_cursor->ConsumeVAddr(partial_update);
          return ZX_ERR_NO_MEMORY;
        }

        LTRACEF_LEVEL(2, "new table %p at level %d\n", m, static_cast<int>(level));

        UpdateEntry(cm, level, new_cursor->vaddr(), e, X86_VIRT_TO_PHYS(m), interm_flags,
                    /*was_terminal=*/false);
        pt_val = *e;
        pages_++;
      }

      MappingCursor cursor;
      ret = AddMapping(get_next_table_from_entry(pt_val), mmu_flags, lower_level(level),
                       existing_action, *new_cursor, &cursor, cm);
      *new_cursor = cursor;
      DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
      if (ret != ZX_OK) {
        return ret;
      }
    }
  }
  abort.cancel();
  return ZX_OK;
}

// Base case of AddMapping for smallest page size.
zx_status_t X86PageTableBase::AddMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                                           ExistingEntryAction existing_action,
                                           const MappingCursor& start_cursor,
                                           MappingCursor* new_cursor, ConsistencyManager* cm) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size()));

  *new_cursor = start_cursor;

  PtFlags term_flags = terminal_flags(PageTableLevel::PT_L, mmu_flags);

  uint index = vaddr_to_index(PageTableLevel::PT_L, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(e))->state() != vm_page_state::FREE);
    if (IS_PAGE_PRESENT(*e)) {
      if (existing_action == ExistingEntryAction::Error) {
        return ZX_ERR_ALREADY_EXISTS;
      }
    } else {
      UpdateEntry(cm, PageTableLevel::PT_L, new_cursor->vaddr(), e, new_cursor->paddr(), term_flags,
                  /*was_terminal=*/false);
    }
    new_cursor->ConsumePAddr(PAGE_SIZE);
    DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
  }

  return ZX_OK;
}

/**
 * @brief Changes the permissions/caching of the range specified by start_cursor
 *
 * Level must be top_level() when invoked.  The caller must, even on failure,
 * free all pages in the |to_free| list and adjust the |pages_| count.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 */
zx_status_t X86PageTableBase::UpdateMapping(volatile pt_entry_t* table, uint mmu_flags,
                                            PageTableLevel level, const MappingCursor& start_cursor,
                                            MappingCursor* new_cursor, ConsistencyManager* cm) {
  DEBUG_ASSERT(table);
  LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", static_cast<int>(level), start_cursor.vaddr(),
          start_cursor.size());
  DEBUG_ASSERT(check_vaddr(start_cursor.vaddr()));

  if (level == PageTableLevel::PT_L) {
    return UpdateMappingL0(table, mmu_flags, start_cursor, new_cursor, cm);
  }

  zx_status_t ret = ZX_OK;
  *new_cursor = start_cursor;

  PtFlags term_flags = terminal_flags(level, mmu_flags);

  size_t ps = page_size(level);
  uint index = vaddr_to_index(level, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    DEBUG_ASSERT(paddr_to_vm_page(X86_VIRT_TO_PHYS(e))->state() != vm_page_state::FREE);
    pt_entry_t pt_val = *e;
    // Skip unmapped pages (we may encounter these due to demand paging)
    if (!IS_PAGE_PRESENT(pt_val)) {
      new_cursor->SkipEntry(level);
      continue;
    }

    if (IS_LARGE_PAGE(pt_val)) {
      bool vaddr_level_aligned = page_aligned(level, new_cursor->vaddr());
      // If the request covers the entire large page, just change the
      // permissions
      if (vaddr_level_aligned && new_cursor->size() >= ps) {
        UpdateEntry(cm, level, new_cursor->vaddr(), e, paddr_from_pte(level, pt_val),
                    term_flags | X86_MMU_PG_PS, /*was_terminal=*/true);
        new_cursor->ConsumeVAddr(ps);
        DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
        continue;
      }
      // Otherwise, we need to split it
      vaddr_t page_vaddr = new_cursor->vaddr() & ~(ps - 1);
      ret = SplitLargePage(level, page_vaddr, e, cm);
      if (ret != ZX_OK) {
        return ret;
      }
      pt_val = *e;
    }

    MappingCursor cursor;
    volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
    ret = UpdateMapping(next_table, mmu_flags, lower_level(level), *new_cursor, &cursor, cm);
    *new_cursor = cursor;
    if (ret != ZX_OK) {
      return ret;
    }
    DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
    DEBUG_ASSERT(new_cursor->size() == 0 || page_aligned(level, new_cursor->vaddr()));
  }
  return ZX_OK;
}

// Base case of UpdateMapping for smallest page size.
zx_status_t X86PageTableBase::UpdateMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                                              const MappingCursor& start_cursor,
                                              MappingCursor* new_cursor, ConsistencyManager* cm) {
  LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr(), start_cursor.size());
  DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size()));

  *new_cursor = start_cursor;

  PtFlags term_flags = terminal_flags(PageTableLevel::PT_L, mmu_flags);

  uint index = vaddr_to_index(PageTableLevel::PT_L, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    pt_entry_t pt_val = *e;
    // Skip unmapped pages (we may encounter these due to demand paging)
    if (IS_PAGE_PRESENT(pt_val)) {
      UpdateEntry(cm, PageTableLevel::PT_L, new_cursor->vaddr(), e,
                  paddr_from_pte(PageTableLevel::PT_L, pt_val), term_flags,
                  /*was_terminal=*/true);
    }

    new_cursor->ConsumeVAddr(PAGE_SIZE);
    DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
  }
  DEBUG_ASSERT(new_cursor->size() == 0 || page_aligned(PageTableLevel::PT_L, new_cursor->vaddr()));
  return ZX_OK;
}

/**
 * @brief Removes the accessed flag on any terminal entries and calls
 * pmm_page_queues()->MarkAccessed on them. For non-terminal entries any accessed bits are
 * harvested, and unaccessed non-terminal entries are unmapped or retained based on the passed in
 * action.
 *
 * Level must be top_level() when invoked.  The caller must, even on failure,
 * free all pages in the |to_free| list and adjust the |pages_| count.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return true if the caller (i.e. the next level up page table) might need to
 * free this page table.
 */
bool X86PageTableBase::HarvestMapping(volatile pt_entry_t* table,
                                      NonTerminalAction non_terminal_action,
                                      TerminalAction terminal_action, PageTableLevel level,
                                      const MappingCursor& start_cursor, MappingCursor* new_cursor,
                                      ConsistencyManager* cm) {
  DEBUG_ASSERT(table);
  LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", static_cast<int>(level), start_cursor.vaddr(),
          start_cursor.size());
  DEBUG_ASSERT(check_vaddr(start_cursor.vaddr()));

  if (level == PageTableLevel::PT_L) {
    HarvestMappingL0(table, terminal_action, start_cursor, new_cursor, cm);
    // HarvestMappingL0 never actually unmaps any entries, so this is always false.
    return false;
  }

  *new_cursor = start_cursor;

  // Track if we perform any unmappings. We propagate this up to our caller, since if we performed
  // any unmappings then we could now be empty, and if so our caller needs to free us.
  bool unmapped = false;
  size_t ps = page_size(level);
  uint index = vaddr_to_index(level, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    pt_entry_t pt_val = *e;
    // If the page isn't even mapped, just skip it
    if (!IS_PAGE_PRESENT(pt_val)) {
      new_cursor->SkipEntry(level);
      DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
      continue;
    }

    if (IS_LARGE_PAGE(pt_val)) {
      bool vaddr_level_aligned = page_aligned(level, new_cursor->vaddr());
      // If the request covers the entire large page then harvest the accessed bit, otherwise we
      // just skip it.
      if (vaddr_level_aligned && new_cursor->size() >= ps) {
        const uint mmu_flags = pt_flags_to_mmu_flags(pt_val, level);
        const PtFlags term_flags = terminal_flags(level, mmu_flags);
        UpdateEntry(cm, level, new_cursor->vaddr(), e, paddr_from_pte(level, pt_val),
                    term_flags | X86_MMU_PG_PS, /*was_terminal=*/true, /*exact_flags=*/true);
      }
      new_cursor->ConsumeVAddr(ps);
      DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
      continue;
    }

    MappingCursor cursor;
    volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
    paddr_t ptable_phys = X86_VIRT_TO_PHYS(next_table);
    bool lower_unmapped;
    bool unmap_page_table = false;
    if (pt_val & X86_MMU_PG_A) {
      // Entry is accessed, we need to recurse and check for accessed bits at the next level. We
      // unset the AF later should we end up not unmapping the page table.
      lower_unmapped = HarvestMapping(next_table, non_terminal_action, terminal_action,
                                      lower_level(level), *new_cursor, &cursor, cm);
    } else if (non_terminal_action == NonTerminalAction::FreeUnaccessed) {
      auto status = RemoveMapping(next_table, lower_level(level), EnlargeOperation::No, *new_cursor,
                                  &cursor, cm);
      // Although we pass in EnlargeOperation::No, the unmap should never fail since we are
      // unmapping an entire block and never a sub part of a page.
      ASSERT(status.is_ok());
      lower_unmapped = status.value();
      // If we processed the entire next level then we can ignore lower_unmapped and just directly
      // assume that the whole page table is empty/unaccessed and that we can unmap it.
      unmap_page_table = page_aligned(level, new_cursor->vaddr()) && new_cursor->size() >= ps;
    } else {
      // No accessed flag and no request to unmap means we are done with this entry.
      new_cursor->SkipEntry(level);
      DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
      continue;
    }

    // If the lower page table was accessed and there is uncertainty around whether it might now be
    // empty, then we have to just scan it and see.
    if (!unmap_page_table && lower_unmapped) {
      unmap_page_table = page_table_is_clear(next_table);
    }
    if (unmap_page_table) {
      LTRACEF("L: %d free pt v %#" PRIxPTR " phys %#" PRIxPTR "\n", static_cast<int>(level),
              (uintptr_t)next_table, ptable_phys);

      vm_page_t* page = paddr_to_vm_page(ptable_phys);
      DEBUG_ASSERT(page == paddr_to_vm_page(X86_VIRT_TO_PHYS(get_next_table_from_entry(*e))));
      UnmapEntry(cm, level, new_cursor->vaddr(), e, /*was_terminal=*/false);

      DEBUG_ASSERT(page);
      DEBUG_ASSERT_MSG(page->state() == vm_page_state::MMU,
                       "page %p state %u, paddr %#" PRIxPTR "\n", page,
                       static_cast<uint32_t>(page->state()), X86_VIRT_TO_PHYS(next_table));
      DEBUG_ASSERT(!list_in_list(&page->queue_node));

      cm->queue_free(page);
      unmapped = true;
    } else if ((pt_val & X86_MMU_PG_A) && non_terminal_action != NonTerminalAction::Retain) {
      // Since we didn't unmap, we need to unset the accessed flag.
      const IntermediatePtFlags flags = intermediate_flags();
      UpdateEntry(cm, level, new_cursor->vaddr(), e, ptable_phys, flags, /*was_terminal=*/false,
                  /*exact_flags=*/true);
      // For the accessed flag to reliably reset we need to ensure that any leaf pages from here are
      // not in the TLB so that a re-walk occurs. To avoid having to find every leaf page, which
      // will probably exceed the consistency managers into count anyway, force trigger a full
      // shootdown.
      cm->SetFullShootdown();
    }
    *new_cursor = cursor;
    DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
    DEBUG_ASSERT(new_cursor->size() == 0 || page_aligned(level, new_cursor->vaddr()));
  }
  return unmapped;
}

// Base case of HarvestMapping for smallest page size.
void X86PageTableBase::HarvestMappingL0(volatile pt_entry_t* table, TerminalAction terminal_action,
                                        const MappingCursor& start_cursor,
                                        MappingCursor* new_cursor, ConsistencyManager* cm) {
  LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr(), start_cursor.size());
  DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size()));

  *new_cursor = start_cursor;

  uint index = vaddr_to_index(PageTableLevel::PT_L, new_cursor->vaddr());
  for (; index != NO_OF_PT_ENTRIES && new_cursor->size() != 0; ++index) {
    volatile pt_entry_t* e = table + index;
    pt_entry_t pt_val = *e;
    if (IS_PAGE_PRESENT(pt_val) && (pt_val & X86_MMU_PG_A)) {
      const paddr_t paddr = paddr_from_pte(PageTableLevel::PT_L, pt_val);
      const uint mmu_flags = pt_flags_to_mmu_flags(pt_val, PageTableLevel::PT_L);
      const PtFlags term_flags = terminal_flags(PageTableLevel::PT_L, mmu_flags);

      vm_page_t* page = paddr_to_vm_page(paddr);
      // Mappings for physical VMOs do not have pages associated with them and so there's no state
      // to update on an access. As the hardware will update any higher level accessed bits for us
      // we do not even ned to remove the accessed bit in that case.
      if (likely(page)) {
        pmm_page_queues()->MarkAccessedDeferredCount(page);

        if (terminal_action == TerminalAction::UpdateAgeAndHarvest) {
          UpdateEntry(cm, PageTableLevel::PT_L, new_cursor->vaddr(), e,
                      paddr_from_pte(PageTableLevel::PT_L, pt_val), term_flags,
                      /*was_terminal=*/true, /*exact_flags=*/true);
        }
      }
    }

    new_cursor->ConsumeVAddr(PAGE_SIZE);
    DEBUG_ASSERT(new_cursor->size() <= start_cursor.size());
  }
  DEBUG_ASSERT(new_cursor->size() == 0 || page_aligned(PageTableLevel::PT_L, new_cursor->vaddr()));
}

zx_status_t X86PageTableBase::UnmapPages(vaddr_t vaddr, const size_t count,
                                         EnlargeOperation enlarge, size_t* unmapped) {
  LTRACEF("aspace %p, vaddr %#" PRIxPTR ", count %#zx\n", this, vaddr, count);

  canary_.Assert();

  if (!check_vaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;
  if (count == 0)
    return ZX_OK;

  MappingCursor start(/*vaddr=*/vaddr, /*size=*/count * PAGE_SIZE);
  MappingCursor result;

  __UNINITIALIZED ConsistencyManager cm(this);
  // This needs to be initialized to some value as gcc cannot work out that it can elide the default
  // constructor.
  zx::result<bool> status = zx::ok(true);
  {
    Guard<Mutex> a{&lock_};
    DEBUG_ASSERT(virt_);
    status = RemoveMapping(virt_, top_level(), enlarge, start, &result, &cm);
    cm.Finish();
  }
  DEBUG_ASSERT(result.size() == 0 || status.is_error());

  if (unmapped)
    *unmapped = count;

  return status.status_value();
}

zx_status_t X86PageTableBase::MapPages(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                                       ExistingEntryAction existing_action, size_t* mapped) {
  canary_.Assert();

  LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n", this, vaddr, count,
          mmu_flags);

  if (!check_vaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;
  for (size_t i = 0; i < count; ++i) {
    if (!check_paddr(phys[i]))
      return ZX_ERR_INVALID_ARGS;
  }
  if (count == 0)
    return ZX_OK;

  if (!allowed_flags(mmu_flags))
    return ZX_ERR_INVALID_ARGS;

  PageTableLevel top = top_level();
  __UNINITIALIZED ConsistencyManager cm(this);
  {
    Guard<Mutex> a{&lock_};
    DEBUG_ASSERT(virt_);

    MappingCursor start(/*paddrs=*/phys, /*paddr_count=*/count, /*page_size=*/PAGE_SIZE,
                        /*vaddr=*/vaddr, /*size=*/count * PAGE_SIZE);
    MappingCursor result;
    zx_status_t status = AddMapping(virt_, mmu_flags, top, existing_action, start, &result, &cm);
    cm.Finish();
    if (status != ZX_OK) {
      dprintf(SPEW, "Add mapping failed with err=%d\n", status);
      return status;
    }
    DEBUG_ASSERT(result.size() == 0);
  }

  if (mapped) {
    *mapped = count;
  }
  return ZX_OK;
}

zx_status_t X86PageTableBase::MapPagesContiguous(vaddr_t vaddr, paddr_t paddr, const size_t count,
                                                 uint mmu_flags, size_t* mapped) {
  canary_.Assert();

  LTRACEF("aspace %p, vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n", this,
          vaddr, paddr, count, mmu_flags);

  if (!check_paddr(paddr))
    return ZX_ERR_INVALID_ARGS;
  if (!check_vaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;
  if (count == 0)
    return ZX_OK;

  if (!allowed_flags(mmu_flags))
    return ZX_ERR_INVALID_ARGS;

  MappingCursor start(/*paddrs=*/&paddr, /*paddr_count=*/1, /*page_size=*/count * PAGE_SIZE,
                      /*vaddr=*/vaddr, /*size=*/count * PAGE_SIZE);
  MappingCursor result;
  __UNINITIALIZED ConsistencyManager cm(this);
  {
    Guard<Mutex> a{&lock_};
    DEBUG_ASSERT(virt_);
    zx_status_t status =
        AddMapping(virt_, mmu_flags, top_level(), ExistingEntryAction::Error, start, &result, &cm);
    cm.Finish();
    if (status != ZX_OK) {
      dprintf(SPEW, "Add mapping failed with err=%d\n", status);
      return status;
    }
  }
  DEBUG_ASSERT(result.size() == 0);

  if (mapped)
    *mapped = count;

  return ZX_OK;
}

zx_status_t X86PageTableBase::ProtectPages(vaddr_t vaddr, size_t count, uint mmu_flags) {
  canary_.Assert();

  LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n", this, vaddr, count,
          mmu_flags);

  if (!check_vaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;
  if (count == 0)
    return ZX_OK;

  if (!allowed_flags(mmu_flags))
    return ZX_ERR_INVALID_ARGS;

  MappingCursor start(/*vaddr=*/vaddr, /*size=*/count * PAGE_SIZE);
  MappingCursor result;
  __UNINITIALIZED ConsistencyManager cm(this);
  {
    Guard<Mutex> a{&lock_};
    zx_status_t status = UpdateMapping(virt_, mmu_flags, top_level(), start, &result, &cm);
    cm.Finish();
    if (status != ZX_OK) {
      return status;
    }
  }
  DEBUG_ASSERT(result.size() == 0);
  return ZX_OK;
}

zx_status_t X86PageTableBase::QueryVaddr(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
  canary_.Assert();

  PageTableLevel ret_level;

  LTRACEF("aspace %p, vaddr %#" PRIxPTR ", paddr %p, mmu_flags %p\n", this, vaddr, paddr,
          mmu_flags);

  Guard<Mutex> a{&lock_};

  volatile pt_entry_t* last_valid_entry;
  zx_status_t status = GetMapping(virt_, vaddr, top_level(), &ret_level, &last_valid_entry);
  if (status != ZX_OK)
    return status;

  DEBUG_ASSERT(last_valid_entry);
  LTRACEF("last_valid_entry (%p) 0x%" PRIxPTE ", level %d\n", last_valid_entry, *last_valid_entry,
          static_cast<int>(ret_level));

  /* based on the return level, parse the page table entry */
  if (paddr) {
    switch (ret_level) {
      case PageTableLevel::PDP_L: /* 1GB page */
        *paddr = paddr_from_pte(PageTableLevel::PDP_L, *last_valid_entry);
        *paddr |= vaddr & PAGE_OFFSET_MASK_HUGE;
        break;
      case PageTableLevel::PD_L: /* 2MB page */
        *paddr = paddr_from_pte(PageTableLevel::PD_L, *last_valid_entry);
        *paddr |= vaddr & PAGE_OFFSET_MASK_LARGE;
        break;
      case PageTableLevel::PT_L: /* 4K page */
        *paddr = paddr_from_pte(PageTableLevel::PT_L, *last_valid_entry);
        *paddr |= vaddr & PAGE_OFFSET_MASK_4KB;
        break;
      default:
        panic("arch_mmu_query: unhandled frame level\n");
    }

    LTRACEF("paddr %#" PRIxPTR "\n", *paddr);
  }

  /* converting arch-specific flags to mmu flags */
  if (mmu_flags) {
    *mmu_flags = pt_flags_to_mmu_flags(*last_valid_entry, ret_level);
  }

  return ZX_OK;
}

zx_status_t X86PageTableBase::HarvestAccessed(vaddr_t vaddr, size_t count,
                                              NonTerminalAction non_terminal_action,
                                              TerminalAction terminal_action) {
  canary_.Assert();

  LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx\n", this, vaddr, count);

  if (!check_vaddr(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (count == 0) {
    return ZX_OK;
  }

  MappingCursor start(/*vaddr=*/vaddr, /*size=*/count * PAGE_SIZE);
  MappingCursor result;
  __UNINITIALIZED ConsistencyManager cm(this);
  {
    Guard<Mutex> a{&lock_};
    HarvestMapping(virt_, non_terminal_action, terminal_action, top_level(), start, &result, &cm);
    cm.Finish();
  }
  DEBUG_ASSERT(result.size() == 0);
  return ZX_OK;
}

void X86PageTableBase::Destroy(vaddr_t base, size_t size) {
  canary_.Assert();

  if constexpr (DEBUG_ASSERT_IMPLEMENTED) {
    PageTableLevel top = top_level();
    if (virt_) {
      pt_entry_t* table = static_cast<pt_entry_t*>(virt_);
      uint start = vaddr_to_index(top, base);
      uint end = vaddr_to_index(top, base + size - 1);

      // Check the end if it fills out the table entry.
      if (page_aligned(top, base + size)) {
        end += 1;
      }

      for (uint i = start; i < end; ++i) {
        DEBUG_ASSERT(!IS_PAGE_PRESENT(table[i]));
      }
    }
  }

  if (phys_) {
    pmm_free_page(paddr_to_vm_page(phys_));
    phys_ = 0;
  }
}
