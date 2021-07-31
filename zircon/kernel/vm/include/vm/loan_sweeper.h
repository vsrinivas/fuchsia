// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_LOAN_SWEEPER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_LOAN_SWEEPER_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <sys/types.h>
#include <zircon/time.h>
#include <cstdint>

#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include "ppb_config.h"
#include "vm_page_list.h"

class PageQueues;
class PmmNode;

// PlbSweeper - Physical loaning and borrowing sweeper.
//
// When triggered, this class replaces physical pages used by VmCowPages.  By default, the old page
// is not loaned and the new page is loaned.
//
// Using k plb, the behavior can be changed to replace loaned pages with non-loaned pages.
//
// This class is triggered by transition to OOM WARNING (once).
//
// In addition, while OOM signals are indicating CRITICAL or worse, this class is triggered
// incrementally when it may be possible to match up free loaned physical pages with VMOs that have
// non-pinned non-loaned physical pages, freeing up the non-loaned physical pages for general use,
// and hopefully exiting OOM CRITICAL state.  This incremental behavior is only done when we're in
// OOM CRITICAL (or worse) state, because it can potentially churn with pinning/unpinning.  We try
// to avoid too much churn by avoiding borrowing by VMO offsets that were quite recently pinned.
//
// Separately, whenver borrowing is enabled, pmm will prefer to allocate loaned pages for new
// allocations.  In a best-effort fashion during allocation, we try to place a run of contiguous
// loaned physical pages in a VmPageListNode, and we track whether a VmPageListNode has any borrowed
// physical pages, to avoid updating per-page backlinks for non-pager-backed VmPageListNode(s) that
// have zero borrowed pages.  By always preferring to use loaned pages, we reduce the amount of
// page content copying that occurs when we enter OOM WARNING state, as we're already preferring to
// use loaned pages up to that point.  In addition, we avoid the system suddenly changing it's page
// copying costs (such as during pin) only when the system is under load, since we don't want CPU
// usage to suddenly increase (even a little) due to increased memory usage; instead it's better if
// we incur the page copying behavior even before we hit OOM WARNING.
//
// DO NOT SUBMIT - The policies described above are meant to be a reasonable starting point; additional/other ideas welcome.
class LoanSweeper {
 public:
  explicit LoanSweeper(PmmNode *node);
  ~LoanSweeper();

  // Must be called before any continuous sweeping will happen.
  void Init();

  uint64_t SynchronousSweep(bool is_continuous_sweep, bool also_replace_recently_pinned);
  void EnableContinuousSweep();
  void DisableContinuousSweep();

  uint64_t ForceSynchronousSweep(bool is_continuous_sweep, bool also_replace_recently_pinned);
 private:
  // This and kUnpinTimeResolutionNanos are tuned to incorrectly identify pages as
  // too-recently-unpinned <= 1/16th of the time.
  static constexpr zx_duration_t kContinuousSweepUnpinAgeThreshold = ZX_MSEC(1500);

  // Private constructor for test code to specify |queues| not owned by |node|.
  LoanSweeper(PmmNode *node, PageQueues *queues, PpbConfig* config);
  friend class vm_unittest::TestPmmNode;

  // The main loop for the loan sweeper thread.
  int ThreadLoop() TA_EXCL(lock_);

  uint64_t SynchronousSweepInternal(bool is_continuous_sweep, bool also_replace_recently_pinned) TA_EXCL(lock_);

  mutable DECLARE_MUTEX(LoanSweeper) lock_;

  // The loan sweeper thread used to process asynchronous requests.  The thread is created by EnableLoanSweeping().
  Thread *thread_ = nullptr;

  // Event that blocks ThreadLoop unless continuous sweep is currently enabled, or the thread needs to exit.
  Event unblock_thread_loop_;

  // Used by the loan sweeper thread to be able to quickly exit from what would otherwise be a sleep.
  Event skip_sleep_signal_;

  // The PmmNode whose free level the LoanSweeper monitors, and frees pages to.
  PmmNode *const pmm_node_;

  // The set of PageQueues that the LoanSweeper uses to find non-loaned pages to replace with loaned
  // pages.
  //
  // This is technically not needed and is mostly for the benefit of unit tests. The LoanSweeper can
  // just call pmm_node_->GetPageQueues() to get the right set of page queues to work on. However,
  // the VMO side code is currently PmmNode agnostic, and until there exists a way for VMOs to
  // allocate from (and free to) a particular PmmNode, we'll need to track the PageQueues separately
  // in order to write meaningful tests.
  //
  // This is set to pmm_node_->GetPageQueues() by the public constructor that passes in the PmmNode
  // associated with this evictor. The private constructor which also passes in PageQueues (not
  // necessarily owned by the PmmNode) is only used in test code.
  PageQueues *const page_queues_;

  PpbConfig *const ppb_config_;

  // Initialized in Init().
  size_t num_arenas_;
  ktl::unique_ptr<pmm_arena_info_t[]> arenas_;
  paddr_t min_paddr_;
  paddr_t max_paddr_;
  paddr_t next_start_paddr_;

  ktl::atomic<bool> continuous_sweep_enabled_;
  ktl::atomic<bool> exiting_;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_LOAN_SWEEEPER_H_
