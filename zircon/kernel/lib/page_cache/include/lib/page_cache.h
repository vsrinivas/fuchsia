// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_PAGE_CACHE_INCLUDE_LIB_PAGE_CACHE_H_
#define ZIRCON_KERNEL_LIB_PAGE_CACHE_INCLUDE_LIB_PAGE_CACHE_H_

#include <inttypes.h>
#include <lib/ktrace.h>
#include <lib/zx/result.h>
#include <trace.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>

#include <new>

#include <arch/defines.h>
#include <arch/ops.h>
#include <fbl/alloc_checker.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <vm/page_state.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

// PageCache provides a front end to the PMM that reserves a given number of
// pages in per-CPU caches to reduce contention on the PMM.
//
// TODO(fxbug.dev/68453): Add support for KASAN.
// TODO(fxbug.dev/68455): Flush page caches when CPUs go offline.
//

namespace page_cache {

class PageCache {
  static constexpr bool kTraceEnabled = false;

  using LocalTraceDuration =
      TraceDuration<TraceEnabled<kTraceEnabled>, KTRACE_GRP_SCHEDULER, TraceContext::Thread>;

 public:
  // Creates a page cache with the given number of reserve pages per CPU.
  // Filling the per-CPU page caches is deferred until the first allocation
  // request.
  static zx::result<PageCache> Create(size_t reserve_pages);

  PageCache() = default;
  ~PageCache() = default;

  // PageCache is not copiable.
  PageCache(const PageCache&) = delete;
  PageCache& operator=(const PageCache&) = delete;

  // PageCache is movable.
  PageCache(PageCache&&) = default;
  PageCache& operator=(PageCache&&) = default;

  // Returns true if this PageCache instance is non-empty.
  explicit operator bool() const { return bool(per_cpu_caches_); }

  // Utility type for returning a list of pages via zx::result. Automatically
  // frees a non-empty list of pages on destruction to improve safety and
  // ergonomics.
  struct PageList : list_node {
    PageList() : list_node{this, this} {}

    ~PageList() {
      if (!is_empty()) {
        pmm_free(this);
      }
    }

    PageList(list_node&& other) { list_move(&other, this); }

    PageList(PageList&& other) noexcept { list_move(&other, this); }

    PageList& operator=(PageList&& other) noexcept {
      list_node temp = LIST_INITIAL_VALUE(temp);
      list_move(&other, &temp);

      if (!is_empty()) {
        pmm_free(this);
      }

      list_move(&temp, this);
      return *this;
    }

    bool is_empty() const { return list_is_empty(this); }

    PageList(const PageList&) = delete;
    PageList& operator=(const PageList&) = delete;
  };

  struct AllocateResult {
    // The list of pages allocated by the request.
    PageList page_list{};

    // The number of pages remaining in the cache after the request.
    size_t available_pages{0};
  };

  // Allocates the given number of pages from the page cache. Falls back to the
  // PMM if the cache is insufficient to fulfill the request.
  zx::result<AllocateResult> Allocate(size_t page_count, uint alloc_flags = 0) {
    LocalTraceDuration trace{"PageCache::Allocate"_stringref};
    DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());
    DEBUG_ASSERT(per_cpu_caches_ != nullptr);

    // Fall back to the PMM for low mem/loaned pages.
    if (alloc_flags &
        (PMM_ALLOC_FLAG_LO_MEM | PMM_ALLOC_FLAG_MUST_BORROW | PMM_ALLOC_FLAG_CAN_BORROW)) {
      list_node page_list = LIST_INITIAL_VALUE(page_list);
      const zx_status_t status = pmm_alloc_pages(page_count, alloc_flags, &page_list);
      if (status != ZX_OK) {
        return zx::error_result(status);
      }
      return zx::ok(AllocateResult{ktl::move(page_list), page_count});
    }

    AutoPreemptDisabler preempt_disable;
    const cpu_num_t current_cpu = arch_curr_cpu_num();
    return Allocate(per_cpu_caches_[current_cpu], page_count, alloc_flags);
  }

  // Returns the given pages to the page cache. Excess pages are returned to the
  // PMM.
  void Free(PageList page_list) {
    LocalTraceDuration trace{"PageCache::Free"_stringref};
    DEBUG_ASSERT(per_cpu_caches_ != nullptr);

    if (!page_list.is_empty()) {
      // Note that preempt_disable is destroyed before page_list, intentionally
      // resulting in excess pages being freed outside of this local preemption
      // disablement.
      AutoPreemptDisabler preempt_disable;
      const cpu_num_t current_cpu = arch_curr_cpu_num();
      Free(per_cpu_caches_[current_cpu], &page_list);
    }
  }

  size_t reserve_pages() const { return reserve_pages_; }

 private:
  struct alignas(MAX_CACHE_LINE) CpuCache {
    DECLARE_MUTEX(CpuCache) fill_lock;
    DECLARE_MUTEX(CpuCache) cache_lock;

    TA_GUARDED(cache_lock)
    size_t available_pages{0};

    TA_GUARDED(cache_lock)
    PageList free_list;
  };

  explicit PageCache(size_t reserve_pages, ktl::unique_ptr<CpuCache[]> entries)
      : reserve_pages_{reserve_pages}, per_cpu_caches_{ktl::move(entries)} {}

  static void CountHitPages(size_t page_count);
  static void CountMissPages(size_t page_count);
  static void CountRefillPages(size_t page_count);
  static void CountReturnPages(size_t page_count);
  static void CountFreePages(size_t page_count);

  // Attempts to allocate the given number of pages from the CPU cache. If the
  // cache is insufficient for the request, falls back to the PMM to fulfill the
  // request and refill the cache. The requested number of pages may be zero, in
  // which case only the cache is filled.
  zx::result<AllocateResult> Allocate(CpuCache& entry, size_t requested_pages, uint alloc_flags)
      TA_EXCL(entry.fill_lock, entry.cache_lock) {
    if (requested_pages > 0) {
      Guard<Mutex> guard{&entry.cache_lock};
      if (requested_pages <= entry.available_pages) {
        return zx::ok(AllocateCachePages(entry, requested_pages));
      }
    }
    return AllocatePagesAndFillCache(entry, requested_pages, alloc_flags);
  }

  // Allocates the given number of pages from the given CPU cache.
  static AllocateResult AllocateCachePages(CpuCache& entry, size_t requested_pages)
      TA_REQ(entry.cache_lock) {
    DEBUG_ASSERT(requested_pages <= entry.available_pages);
    DEBUG_ASSERT(requested_pages > 0);

    entry.available_pages -= requested_pages;
    list_node* node = list_prev(&entry.free_list, &entry.free_list);
    for (size_t i = 0; i < requested_pages; i++) {
      vm_page* page = containerof(node, vm_page, queue_node);
      page->set_state(vm_page_state::ALLOC);
      node = list_prev(&entry.free_list, node);
    }

    CountHitPages(requested_pages);

    list_node return_pages = LIST_INITIAL_VALUE(return_pages);
    list_split_after(&entry.free_list, node ? node : &entry.free_list, &return_pages);

    return AllocateResult{ktl::move(return_pages), entry.available_pages};
  }

  // Returns the given list of pages to the given CPU cache, returning excess
  // pages to the PMM.
  void Free(CpuCache& entry, PageList* page_list) const TA_EXCL(entry.fill_lock, entry.cache_lock) {
    Guard<Mutex> guard{&entry.cache_lock};

    size_t free_count = 0;
    size_t return_count = 0;
    list_node return_list = LIST_INITIAL_VALUE(return_list);

    // Filter out non-borrowed pages to return to the free list. Pages remaining
    // in page_list are freed to the PMM outside the lock in the PageList dtor.
    list_node* node;
    list_node* temp;
    list_for_every_safe(page_list, node, temp) {
      vm_page* page = containerof(node, vm_page, queue_node);
      if (entry.available_pages < reserve_pages_ && !page->is_loaned()) {
        page->set_state(vm_page_state::CACHE);

        list_delete(node);
        list_add_tail(&return_list, node);

        entry.available_pages++;
        return_count++;
      } else {
        free_count++;
      }
    }

    // Return the selected pages to the free list.
    list_splice_after(&return_list, &entry.free_list);
    CountReturnPages(return_count);
    CountFreePages(free_count);
  }

  zx::result<AllocateResult> AllocatePagesAndFillCache(CpuCache& entry, size_t requested_pages,
                                                       uint alloc_flags) const
      TA_EXCL(entry.fill_lock, entry.cache_lock) {
    LocalTraceDuration trace{"PageCache::AllocatePagesAndFillCache"_stringref};

    // Serialize cache fill + allocate operations on this cache. Contention
    // means another thread tried to allocate from the PMM and blocked on the
    // PMM lock. There's no benefit to following the owning thread into the PMM
    // allocator, by the time this lock is released the cache may have enough
    // pages to satisfy this request without falling back to the PMM again.
    Guard<Mutex> fill_guard{&entry.fill_lock};

    // Acquire the cache lock after the fill lock. If this thread blocked on the
    // previous lock, there is a chance this thread is now running on a
    // different CPU. However, there's also a good chance the cache is now
    // sufficient to fulfill the request without falling back to the PMM.
    Guard<Mutex> cache_guard{&entry.cache_lock};

    list_node return_list = LIST_INITIAL_VALUE(return_list);

    // Re-validate the request after acquiring the locks. Another thread may
    // have filled the cache sufficiently already.
    if (requested_pages > entry.available_pages) {
      const size_t refill_pages = reserve_pages_ - entry.available_pages;
      const size_t total_pages = requested_pages + refill_pages;

      CountRefillPages(refill_pages);
      CountMissPages(requested_pages);

      list_node page_list = LIST_INITIAL_VALUE(page_list);
      zx_status_t status;

      // Release the cache lock while calling into the PMM to permit other
      // threads to access the cache if this thread blocks.
      cache_guard.CallUnlocked([total_pages, alloc_flags, &page_list, &status]() {
        status = pmm_alloc_pages(total_pages, alloc_flags, &page_list);
      });
      if (status != ZX_OK) {
        return zx::error_result(status);
      }

      // Set the page state of the refill pages and find the end of the refill list.
      list_node* node = &page_list;
      for (size_t i = 0; i < refill_pages; i++) {
        node = list_next(&page_list, node);
        vm_page* page = containerof(node, vm_page, queue_node);
        page->set_state(vm_page_state::CACHE);
      }
      DEBUG_ASSERT(node != nullptr);

      // Separate the return list from the refill list and add the latter to the
      // free list.
      list_split_after(&page_list, node, &return_list);
      list_splice_after(&page_list, &entry.free_list);

      entry.available_pages += refill_pages;
    } else if (requested_pages > 0) {
      return zx::ok(AllocateCachePages(entry, requested_pages));
    }

    return zx::ok(AllocateResult{ktl::move(return_list), entry.available_pages});
  }

  size_t reserve_pages_{0};
  ktl::unique_ptr<CpuCache[]> per_cpu_caches_{};
};

}  // namespace page_cache

#endif  // ZIRCON_KERNEL_LIB_PAGE_CACHE_INCLUDE_LIB_PAGE_CACHE_H_
