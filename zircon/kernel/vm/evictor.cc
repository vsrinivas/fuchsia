// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/cmdline.h>
#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/macros.h>

#include <cassert>
#include <cstdint>

#include <kernel/lockdep.h>
#include <ktl/algorithm.h>
#include <vm/evictor.h>
#include <vm/pmm.h>
#include <vm/scanner.h>
#include <vm/vm_cow_pages.h>

KCOUNTER(pager_backed_pages_evicted, "vm.reclamation.pages_evicted_pager_backed")
KCOUNTER(discardable_pages_evicted, "vm.reclamation.pages_evicted_discardable")

bool Evictor::IsEvictionEnabled() const {
  Guard<SpinLock, IrqSave> guard{&lock_};
  return eviction_enabled_;
}

void Evictor::SetEvictionEnabled(bool eviction_enabled) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  eviction_enabled_ = eviction_enabled;
}

void Evictor::SetDiscardableEvictionsPercent(uint32_t discardable_percent) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  if (discardable_percent <= 100) {
    discardable_evictions_percent_ = discardable_percent;
  }
}

void Evictor::SetOneShotEvictionTarget(EvictionTarget target) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  one_shot_eviction_target_ = target;
}

void Evictor::CombineOneShotEvictionTarget(EvictionTarget target) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  one_shot_eviction_target_.pending = one_shot_eviction_target_.pending || target.pending;
  one_shot_eviction_target_.level = ktl::max(one_shot_eviction_target_.level, target.level);
  one_shot_eviction_target_.min_pages_to_free += target.min_pages_to_free;
  one_shot_eviction_target_.free_pages_target =
      ktl::max(one_shot_eviction_target_.free_pages_target, target.free_pages_target);
}

Evictor::EvictedPageCounts Evictor::EvictOneShotFromPreloadedTarget() {
  EvictedPageCounts total_evicted_counts = {};

  // Create a local copy of the eviction target to operate against.
  EvictionTarget target;
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    target = one_shot_eviction_target_;
    one_shot_eviction_target_ = {};
  }
  if (!target.pending) {
    return total_evicted_counts;
  }

  return EvictUntilTargetsMet(target.min_pages_to_free, target.free_pages_target, target.level);
}

uint64_t Evictor::EvictOneShotSynchronous(uint64_t min_mem_to_free, EvictionLevel eviction_level,
                                          Output output) {
  if (!IsEvictionEnabled()) {
    return 0;
  }
  SetOneShotEvictionTarget(EvictionTarget{
      .pending = true,
      // No target free pages to get to. Evict based only on the min pages requested to evict.
      .free_pages_target = 0,
      // For synchronous eviction, set the eviction level and min target as requested.
      .min_pages_to_free = min_mem_to_free / PAGE_SIZE,
      .level = eviction_level,
  });

  auto evicted_counts = EvictOneShotFromPreloadedTarget();

  if (output == Output::Print && evicted_counts.pager_backed > 0) {
    printf("[EVICT]: Evicted %lu user pager backed pages\n", evicted_counts.pager_backed);
  }
  if (output == Output::Print && evicted_counts.discardable > 0) {
    printf("[EVICT]: Evicted %lu pages from discardable vmos\n", evicted_counts.discardable);
  }
  return evicted_counts.pager_backed + evicted_counts.discardable;
}

Evictor::EvictedPageCounts Evictor::EvictUntilTargetsMet(uint64_t min_pages_to_evict,
                                                         uint64_t free_pages_target,
                                                         EvictionLevel level) {
  EvictedPageCounts total_evicted_counts = {};
  if (!IsEvictionEnabled()) {
    return total_evicted_counts;
  }

  // Wait until no eviction attempts are ongoing, so that we don't overshoot the free pages target.
  no_ongoing_eviction_.Wait(Deadline::infinite());
  auto signal_cleanup = fit::defer([&]() {
    // Unblock any waiting eviction requests.
    no_ongoing_eviction_.Signal();
  });

  uint64_t total_pages_freed = 0;

  while (true) {
    const uint64_t free_pages = pmm_count_free_pages();
    uint64_t pages_to_free = 0;
    if (total_pages_freed < min_pages_to_evict) {
      pages_to_free = min_pages_to_evict - total_pages_freed;
    } else if (free_pages < free_pages_target) {
      pages_to_free = free_pages_target - free_pages;
    } else {
      // The targets have been met. No more eviction is required right now.
      break;
    }

    // Compute the desired number of discardable pages to free (vs pager-backed).
    uint64_t pages_to_free_discardable = 0;
    {
      Guard<SpinLock, IrqSave> guard{&lock_};
      DEBUG_ASSERT(discardable_evictions_percent_ <= 100);
      pages_to_free_discardable = pages_to_free * discardable_evictions_percent_ / 100;
    }

    uint64_t pages_freed = EvictDiscardable(pages_to_free_discardable);
    total_evicted_counts.discardable += pages_freed;
    total_pages_freed += pages_freed;

    // Free pager backed memory to get to |pages_to_free|.
    uint64_t pages_to_free_pager_backed = pages_to_free - pages_freed;

    uint64_t pages_freed_pager_backed = EvictPagerBacked(pages_to_free_pager_backed, level);
    total_evicted_counts.pager_backed += pages_freed_pager_backed;
    total_pages_freed += pages_freed_pager_backed;

    pages_freed += pages_freed_pager_backed;

    // Should we fail to free any pages then we give up and consider the eviction request complete.
    if (pages_freed == 0) {
      break;
    }
  }

  return total_evicted_counts;
}

uint64_t Evictor::EvictDiscardable(uint64_t target_pages) const {
  if (!IsEvictionEnabled()) {
    return 0;
  }

  list_node_t freed_list;
  list_initialize(&freed_list);

  // Reclaim |target_pages| from discardable vmos that have been reclaimable for at least 10
  // seconds.
  uint64_t count =
      VmCowPages::ReclaimPagesFromDiscardableVmos(target_pages, ZX_SEC(10), &freed_list);
  pmm_free(&freed_list);

  discardable_pages_evicted.Add(count);
  return count;
}

uint64_t Evictor::EvictPagerBacked(uint64_t target_pages, EvictionLevel eviction_level) const {
  if (!IsEvictionEnabled()) {
    return 0;
  }
  uint64_t count = 0;
  list_node_t freed_list;
  list_initialize(&freed_list);

  while (count < target_pages) {
    // Avoid evicting from the newest queue to prevent thrashing.
    const size_t lowest_evict_queue =
        eviction_level == EvictionLevel::IncludeNewest ? 1 : PageQueues::kNumPagerBacked - 1;
    if (ktl::optional<PageQueues::VmoBacklink> backlink =
            pmm_page_queues()->PeekPagerBacked(lowest_evict_queue)) {
      if (!backlink->cow) {
        continue;
      }
      if (backlink->cow->EvictPage(backlink->page, backlink->offset)) {
        list_add_tail(&freed_list, &backlink->page->queue_node);
        count++;
      }
    } else {
      break;
    }
  }

  pmm_free(&freed_list);

  pager_backed_pages_evicted.Add(count);
  return count;
}
