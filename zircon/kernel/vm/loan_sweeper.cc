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
#include <vm/loan_sweeper.h>
#include <vm/pmm.h>
#include <vm/scanner.h>
#include <vm/vm_cow_pages.h>

#include "pmm_node.h"

KCOUNTER(sweep_count, "vm.reclamation.sweep_count")
KCOUNTER(sweep_looped, "vm.reclamation.sweep_looped")
KCOUNTER(sweep_pages_examined, "vm.reclamation.sweep_pages_examined")
KCOUNTER(sweep_pages_swept_to_loaned, "vm.reclamation.sweep_pages_swept_to_loaned")
KCOUNTER(sweep_page_chase_retried, "vm.reclamation.sweep_page_chase_retried")
KCOUNTER(sweep_page_chase_gave_up, "vm.reclamation.sweep_page_chase_gave_up")

LoanSweeper::LoanSweeper()
    : page_queues_(pmm_page_queues()), ppb_config_(pmm_physical_page_borrowing_config()) {}

LoanSweeper::LoanSweeper(PageQueues* queues, PhysicalPageBorrowingConfig* config)
    : page_queues_(queues), ppb_config_(config) {}

void LoanSweeper::Init() {
  num_arenas_ = pmm_num_arenas();
  fbl::AllocChecker ac;
  arenas_ = ktl::make_unique<pmm_arena_info[]>(&ac, num_arenas_);
  // This allocation happens super early, and only super early, so require it to succeed.  If it
  // doesn't succeed, most likely something has gone quite wrong quite early.
  ASSERT(ac.check());

  zx_status_t status =
      pmm_get_arena_info(num_arenas_, /*i=*/0, &arenas_[0], num_arenas_ * sizeof(arenas_[0]));
  // The only failures are caller bugs, but also check in release in case that changes.
  ASSERT(status == ZX_OK);

  min_paddr_ = ktl::numeric_limits<paddr_t>::max();
  max_paddr_ = ktl::numeric_limits<paddr_t>::min();
  for (uint32_t i = 0; i < num_arenas_; ++i) {
    auto& arena = arenas_[i];
    min_paddr_ = ktl::min(min_paddr_, arena.base);
    max_paddr_ = ktl::max(max_paddr_, arena.base + arena.size - 1);
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    for (uint32_t j = 0; j < num_arenas_; ++j) {
      auto& arena_2 = arenas_[j];
      if (&arena_2 != &arena) {
        // Failing this assert would mean two arenas overlap on the same physical range, which would
        // break assumptions elsewhere in LoanSweeper.
        DEBUG_ASSERT(arena_2.base + arena_2.size - 1 < arena.base ||
                     arena_2.base > arena.base + arena.size - 1);
      }
    }
#endif
  }
  next_start_paddr_ = min_paddr_;
}

uint64_t LoanSweeper::ForceSynchronousSweep() { return SynchronousSweepInternal(); }

// For now, we don't expect the number of loaned pages to typically exceed the number of non-loaned
// non-pinned pages (replaceable pages, roughly speaking) so it's reasonable enough for now to just
// sweep the pmm's page array looking for non-loaned non-pinned used pages when we have free loaned
// pages available.  In the event that there are so many pinned pages that we run out of replaceable
// pages before we run out of loaned pages, we'll end up scanning the whole pmm page array and find
// nothing.  In that event, we'll count the occurrence for now.
//
// Other than too many pages pinned to be able to make use of all loaned pages, we expect the
// density of replace-able pages to be high enough that sweeping in physical order is amortized
// reasonably efficient.
//
// We sweep from a starting offset that's persistent from the end of last sweep, since typically
// any sweeps due to low free pages will end early when we exhaust all loaned pages, and there's a
// better chance of finding replace-able non-loaned pages when we start from where we left off.
uint64_t LoanSweeper::SynchronousSweepInternal() {
  // Sweep (up to) all the pages to find any VMO pages we can move to loaned physical pages, while
  // we have any free loaned physical pages available.
  //
  // We iterate in physical page order because the info we need is in the pmm physical page array,
  // not in VmCowPages.  For now, there's no particular reason to expect a VmPageListNode to
  // typically contain physically-contiguous pages, so we'd be jumping around in the pmm physical
  // page array if we iterated in VmCowPages order.  Non-sequential access is only done for pages we
  // can probably replace with a loaned physical page.
  sweep_count.Add(1);
  pmm_arena_info_t* cached_arena = nullptr;
  for (uint32_t i = 0; i < num_arenas_; ++i) {
    auto& arena = arenas_[i];
    if (arena.base <= next_start_paddr_ && next_start_paddr_ < arena.base + arena.size) {
      cached_arena = &arena;
      break;
    }
  }
  auto get_next_iter = [this, &cached_arena](paddr_t iter) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(iter));
    iter += PAGE_SIZE;
    if (cached_arena && iter < cached_arena->base + cached_arena->size) {
      return iter;
    }
    pmm_arena_info_t* next_arena = nullptr;
    pmm_arena_info_t* min_arena = &arenas_[0];
    for (uint32_t i = 0; i < num_arenas_; ++i) {
      auto& arena = arenas_[i];
      if (arena.base >= iter && (!next_arena || arena.base < next_arena->base)) {
        next_arena = &arena;
      }
      if (arena.base < min_arena->base) {
        min_arena = &arena;
      }
    }
    if (!next_arena) {
      next_arena = min_arena;
    }
    cached_arena = next_arena;
    return next_arena->base;
  };
  bool ppb_enabled = ppb_config_->is_any_borrowing_enabled();
  uint64_t replaced_non_loaned_page_count = 0;
  paddr_t iter;
  auto set_next_start_addr = fit::defer([this, &iter] { next_start_paddr_ = iter; });
  Guard<Mutex> guard(&lock_);
  bool first = true;
  for (iter = next_start_paddr_; iter != next_start_paddr_ || first; iter = get_next_iter(iter)) {
    first = false;
    if (ppb_enabled) {
      if (!pmm_count_loaned_free_pages()) {
        return replaced_non_loaned_page_count;
      }
    } else {
      if (!pmm_count_loaned_used_pages()) {
        return replaced_non_loaned_page_count;
      }
    }
    vm_page_t* page = paddr_to_vm_page(iter);
    DEBUG_ASSERT(page);
    DEBUG_ASSERT(page->paddr() == iter);
    sweep_pages_examined.Add(1);
    // We're willing to try a limited number of times to chase down a non-loaned page as it moves
    // between VmCowPages, but limit the iteration count since it's not critical that we replace
    // every single non-loaned page we iterate over, as there should typically be plenty of
    // non-loaned replaceable pages to use up all the loaned pages.
    bool replaced = false;
    const uint32_t kMaxPageChaseIterations = 3;
    uint32_t page_try_ordinal;
    for (page_try_ordinal = 0; page_try_ordinal < kMaxPageChaseIterations; ++page_try_ordinal) {
      if (page_try_ordinal != 0) {
        sweep_page_chase_retried.Add(1);
      }
      // These are approximate checks, as we're not holding the PageQueues lock or the pmm lock
      // continuously until we replace the pagel.
      if (page->state() != vm_page_state::OBJECT) {
        // next page
        break;
      }
      if (ppb_enabled == pmm_is_loaned(page)) {
        // next page
        break;
      }
      // That's enough pre-checking to filter out most pages that won't work.  Now try to find the
      // owning VmCowPages and replace this page with a loaned page (or non-loaned page).
      //
      // Despite the efforts of GetCowWithReplaceablePage, we may still find below that a returned
      // VmCowPages doesn't have the page any more, which is the reason for the directly enclosing
      // loop.
      auto maybe_vmo_backlink =
          pmm_page_queues()->GetCowWithReplaceablePage(page, /*owning_cow=*/nullptr);
      if (!maybe_vmo_backlink) {
        // There may not be a backlink if page already became FREE or if the page state wasn't
        // immediately consistent with the page being replaceable (without any waiting).
        //
        // next page
        break;
      }
      auto& vmo_backlink = maybe_vmo_backlink.value();
      // Else GetCowWithReplaceablePage wouldn't have set the optional backlink.
      DEBUG_ASSERT(vmo_backlink.cow_container);
      auto& cow_container = vmo_backlink.cow_container;
      // vmo_backlink.offset is offset in cow
      zx_status_t replace_result =
          cow_container->ReplacePage(page, vmo_backlink.offset,
                                     /*with_loaned=*/ppb_enabled ? true : false);
      if (replace_result == ZX_ERR_NOT_FOUND) {
        // No longer owned by cow or no longer replaceable.  Go around again to figure out which and
        // continue chasing it down.  We limit the iteration count however, since it's not critical
        // that we catch up with the page here, and we don't want to get stuck on a page that's
        // moving super often, or pinning/unpinning super often.  Counters track times where we
        // tried more than once, and times when we tried max times and still didn't replace the
        // page.
        //
        // this page again
        continue;
      }
      if (replace_result == ZX_ERR_NO_MEMORY) {
        // Out of pages of the appropriate type, so don't try the next page.
        return replaced_non_loaned_page_count;
      }
      if (replace_result != ZX_OK) {
        // Not replaceable after all.
        //
        // next page
        break;
      }
      // The page has been replaced with a different page that doesn't have loan_cancelled set.
      replaced = true;
      // next page
      break;
    }  // page chase loop
    if (page_try_ordinal == kMaxPageChaseIterations) {
      sweep_page_chase_gave_up.Add(1);
    }
    if (replaced && ppb_enabled) {
      ++replaced_non_loaned_page_count;
      sweep_pages_swept_to_loaned.Add(1);
    }
  }
  if (iter == next_start_paddr_) {
    sweep_looped.Add(1);
  }
  return replaced_non_loaned_page_count;
}
