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

#include "physical_page_borrowing_config.h"
#include "vm_page_list.h"

class PageQueues;
class PmmNode;

// LoanSweeper - Physical loaning and borrowing sweeper.
//
// When triggered by k ppb sweep, this class replaces physical pages used by VmCowPages.
//
// This class is not intended to be used for anything other than k ppb sweep for now.  Before we do
// any "active" sweeping, we need to consider integration of sweeping with eviction and zero
// scanning to make sure they don't have any adverse interactions.
//
// If pmm_physical_page_borrowing_config()->is_any_borrowing_enabled(), non-loaned pages are
// replaced with loaned pages (to the extent that free loaned pages are available).
//
// If !pmm_physical_page_borrowing_config()->is_any_borrowing_enabled(), loaned pages are replaced
// with non-loaned pages.
//
// The k ppb enable and k ppb disable commands can be used to switch enabled() to true or false.
class LoanSweeper {
 public:
  LoanSweeper();
  ~LoanSweeper() = default;

  // Must be called before any continuous sweeping will happen.
  void Init();

  uint64_t ForceSynchronousSweep();

 private:
  // Private constructor for test code to specify |queues| not owned by |node|, test config
  // instance.
  LoanSweeper(PageQueues *queues, PhysicalPageBorrowingConfig *config);
  friend class vm_unittest::TestPmmNode;

  uint64_t SynchronousSweepInternal() TA_EXCL(lock_);

  mutable DECLARE_MUTEX(LoanSweeper) lock_;

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

  PhysicalPageBorrowingConfig *const ppb_config_;

  // Initialized in Init().
  size_t num_arenas_;
  ktl::unique_ptr<pmm_arena_info_t[]> arenas_;
  paddr_t min_paddr_;
  paddr_t max_paddr_;
  paddr_t next_start_paddr_;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_LOAN_SWEEPER_H_
