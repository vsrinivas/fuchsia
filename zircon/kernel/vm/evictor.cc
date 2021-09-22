// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
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

#include "pmm_node.h"

KCOUNTER(pager_backed_pages_evicted, "vm.reclamation.pages_evicted_pager_backed")
KCOUNTER(discardable_pages_evicted, "vm.reclamation.pages_evicted_discardable")

Evictor::Evictor(PmmNode* node) : pmm_node_(node), page_queues_(node->GetPageQueues()) {}

Evictor::Evictor(PmmNode* node, PageQueues* queues) : pmm_node_(node), page_queues_(queues) {}

Evictor::~Evictor() {
  if (eviction_thread_) {
    eviction_thread_exiting_ = true;
    eviction_signal_.Signal();
    int res = 0;
    eviction_thread_->Join(&res, ZX_TIME_INFINITE);
    DEBUG_ASSERT(res == 0);
  }
}

bool Evictor::IsEvictionEnabled() const {
  Guard<SpinLock, IrqSave> guard{&lock_};
  return eviction_enabled_;
}

void Evictor::EnableEviction() {
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    eviction_enabled_ = true;

    if (eviction_thread_) {
      return;
    }
  }

  // Set up the eviction thread to process asynchronous one-shot and continuous eviction requests.
  auto eviction_thread = [](void* arg) -> int {
    Evictor* evictor = reinterpret_cast<Evictor*>(arg);
    return evictor->EvictionThreadLoop();
  };
  eviction_thread_ = Thread::Create("eviction-thread", eviction_thread, this, LOW_PRIORITY);
  DEBUG_ASSERT(eviction_thread_);
  eviction_thread_->Resume();
}

void Evictor::SetDiscardableEvictionsPercent(uint32_t discardable_percent) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  if (discardable_percent <= 100) {
    discardable_evictions_percent_ = discardable_percent;
  }
}

void Evictor::DebugSetMinDiscardableAge(zx_time_t age) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  min_discardable_age_ = age;
}

void Evictor::SetContinuousEvictionInterval(zx_time_t eviction_interval) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  default_eviction_interval_ = eviction_interval;
}

Evictor::EvictionTarget Evictor::DebugGetOneShotEvictionTarget() const {
  Guard<SpinLock, IrqSave> guard{&lock_};
  return one_shot_eviction_target_;
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
  one_shot_eviction_target_.print_counts =
      one_shot_eviction_target_.print_counts || target.print_counts;
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

  uint64_t free_pages_before = pmm_node_->CountFreePages();

  total_evicted_counts =
      EvictUntilTargetsMet(target.min_pages_to_free, target.free_pages_target, target.level);

  if (target.print_counts &&
      total_evicted_counts.discardable + total_evicted_counts.pager_backed > 0) {
    printf("[EVICT]: Free memory before eviction was %zuMB and after eviction is %zuMB\n",
           free_pages_before * PAGE_SIZE / MB, pmm_node_->CountFreePages() * PAGE_SIZE / MB);
    if (total_evicted_counts.pager_backed > 0) {
      printf("[EVICT]: Evicted %lu user pager backed pages\n", total_evicted_counts.pager_backed);
    }
    if (total_evicted_counts.discardable > 0) {
      printf("[EVICT]: Evicted %lu pages from discardable vmos\n",
             total_evicted_counts.discardable);
    }
  }

  return total_evicted_counts;
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
      .print_counts = (output == Output::Print),
  });

  auto evicted_counts = EvictOneShotFromPreloadedTarget();
  return evicted_counts.pager_backed + evicted_counts.discardable;
}

void Evictor::EvictOneShotAsynchronous(uint64_t min_mem_to_free, uint64_t free_mem_target,
                                       Evictor::EvictionLevel eviction_level,
                                       Evictor::Output output) {
  if (!IsEvictionEnabled()) {
    return;
  }
  CombineOneShotEvictionTarget(Evictor::EvictionTarget{
      .pending = true,
      .free_pages_target = free_mem_target / PAGE_SIZE,
      .min_pages_to_free = min_mem_to_free / PAGE_SIZE,
      .level = eviction_level,
      .print_counts = (output == Output::Print),
  });
  // Unblock the eviction thread.
  eviction_signal_.Signal();
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

  DEBUG_ASSERT(pmm_node_);

  while (true) {
    const uint64_t free_pages = pmm_node_->CountFreePages();
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

    // If we've already met the current target, continue to the next iteration of the loop.
    if (pages_freed >= pages_to_free) {
      continue;
    }
    DEBUG_ASSERT(pages_to_free > pages_freed);
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

  // Reclaim |target_pages| from discardable vmos that have been reclaimable for at least
  // |min_discardable_age_|.
  zx_time_t min_age;
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    min_age = min_discardable_age_;
  }
  uint64_t count = VmCowPages::ReclaimPagesFromDiscardableVmos(target_pages, min_age, &freed_list);

  DEBUG_ASSERT(pmm_node_);
  pmm_node_->FreeList(&freed_list);

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

  // Avoid evicting from the newest queue to prevent thrashing.
  const size_t lowest_evict_queue =
      eviction_level == EvictionLevel::IncludeNewest ? 1 : PageQueues::kNumPagerBacked - 1;

  // TODO(fxbug.dev/85056): Always follow the hint for now, i.e. protect hinted pages from eviction
  // even in the face of OOM.
  //
  // Desired future behavior:
  // If we're going to include newest pages, ignore eviction hints as well, i.e. also consider
  // evicting pages with always_need set if we encounter them in LRU order.
  const VmCowPages::EvictionHintAction hint_action = VmCowPages::EvictionHintAction::Follow;

  DEBUG_ASSERT(page_queues_);
  while (count < target_pages) {
    // TODO(rashaeqbal): The sequence of actions in PeekPagerBacked() and EvictPage() implicitly
    // guarantee forward progress in this loop, so that we're not stuck trying to evict the same
    // page (i.e. PeekPagerBacked keeps returning the same page). It would be nice to have some
    // explicit checks here (or in PageQueues) to guarantee forward progress. Or we might want to
    // use cursors to iterate the queues instead of peeking the tail each time.
    if (ktl::optional<PageQueues::VmoBacklink> backlink =
            page_queues_->PeekPagerBacked(lowest_evict_queue)) {
      if (!backlink->cow) {
        continue;
      }
      if (backlink->cow->EvictPage(backlink->page, backlink->offset, hint_action)) {
        list_add_tail(&freed_list, &backlink->page->queue_node);
        count++;
      }
    } else {
      break;
    }
  }

  DEBUG_ASSERT(pmm_node_);
  pmm_node_->FreeList(&freed_list);

  pager_backed_pages_evicted.Add(count);
  return count;
}

void Evictor::EnableContinuousEviction(uint64_t min_mem_to_free, uint64_t free_mem_target,
                                       EvictionLevel eviction_level, Output output) {
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    // Combine min target with previously outstanding min target.
    continuous_eviction_target_.min_pages_to_free += min_mem_to_free / PAGE_SIZE;
    continuous_eviction_target_.free_pages_target = free_mem_target / PAGE_SIZE;
    continuous_eviction_target_.level = eviction_level;
    continuous_eviction_target_.print_counts = (output == Output::Print);
    // .pending has no relevance here since eviction is controlled by the eviction interval.

    // Configure eviction to occur at intervals of |default_eviction_interval_|.
    next_eviction_interval_ = default_eviction_interval_;
  }
  // Unblock the eviction thread.
  eviction_signal_.Signal();
}

void Evictor::DisableContinuousEviction() {
  Guard<SpinLock, IrqSave> guard{&lock_};
  continuous_eviction_target_ = {};
  // In the next iteration of the eviction thread loop, we will see this value and block
  // indefinitely.
  next_eviction_interval_ = ZX_TIME_INFINITE;
}

int Evictor::EvictionThreadLoop() {
  while (!eviction_thread_exiting_) {
    // Block until |next_eviction_interval_| is elapsed.
    zx_time_t wait_interval;
    {
      Guard<SpinLock, IrqSave> guard{&lock_};
      wait_interval = next_eviction_interval_;
    }
    eviction_signal_.Wait(Deadline::no_slack(zx_time_add_duration(current_time(), wait_interval)));

    if (eviction_thread_exiting_) {
      break;
    }

    // Process a one-shot target if there is one. This is a no-op and no pages are evicted if no
    // one-shot target is pending.
    auto evicted = EvictOneShotFromPreloadedTarget();

    // In practice either one-shot eviction or continuous eviction will be enabled at a time. We can
    // skip the rest of the loop if we evicted something here, and go back to wait for another
    // request. If both one-shot and continuous modes are used together, at worst we will wait for
    // |next_eviction_interval_| before evicting as required by the continuous mode, which should
    // still be fine.
    if (evicted.discardable + evicted.pager_backed > 0) {
      continue;
    }

    // Read control parameters into local variables under the lock.
    EvictionTarget target;
    {
      Guard<SpinLock, IrqSave> guard{&lock_};
      target = continuous_eviction_target_;
    }

    uint64_t free_pages_before = pmm_node_->CountFreePages();

    evicted =
        EvictUntilTargetsMet(target.min_pages_to_free, target.free_pages_target, target.level);

    uint64_t total_evicted = evicted.discardable + evicted.pager_backed;
    // If no pages were evicted, we don't have any progress to log, or anything to decrement from
    // the min pages target. Skip the rest of the loop.
    if (total_evicted == 0) {
      continue;
    }

    if (target.print_counts) {
      printf("[EVICT]: Free memory before eviction was %zuMB and after eviction is %zuMB\n",
             free_pages_before * PAGE_SIZE / MB, pmm_node_->CountFreePages() * PAGE_SIZE / MB);
      if (evicted.pager_backed > 0) {
        printf("[EVICT]: Evicted %lu user pager backed pages\n", evicted.pager_backed);
      }
      if (evicted.discardable > 0) {
        printf("[EVICT]: Evicted %lu pages from discardable vmos\n", evicted.discardable);
      }
    }

    {
      // Update min pages target based on the number of pages evicted.
      Guard<SpinLock, IrqSave> guard{&lock_};
      if (total_evicted < continuous_eviction_target_.min_pages_to_free) {
        continuous_eviction_target_.min_pages_to_free -= total_evicted;
      } else {
        continuous_eviction_target_.min_pages_to_free = 0;
      }
    }
  }
  return 0;
}
