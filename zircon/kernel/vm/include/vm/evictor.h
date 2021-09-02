// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_EVICTOR_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_EVICTOR_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <sys/types.h>
#include <zircon/time.h>

#include <kernel/event.h>
#include <kernel/spinlock.h>

class PmmNode;
class PageQueues;

namespace vm_unittest {
class TestPmmNode;
}

// Implements page eviction logic to free pages belonging to a PmmNode under memory pressure.
// Provides APIs for
// 1) one-shot eviction, which involves arming an eviction target and triggering eviction
// and
// 2) continuous eviction, which creates a dedicated thread to perform periodic evictions to
// maintain a free memory level.
// This class is thread-safe.
class Evictor {
 public:
  enum class EvictionLevel : uint8_t {
    OnlyOldest = 0,
    IncludeNewest = 1,
  };

  enum class Output : bool {
    Print = true,
    NoPrint = false,
  };

  // Eviction target state is grouped together behind a lock to allow different threads to safely
  // trigger and perform the eviction.
  struct EvictionTarget {
    bool pending = false;
    // The desired value to get |pmm_node_|'s free page count to
    uint64_t free_pages_target = 0;
    // A minimum amount of pages we want to evict, regardless of how much free memory is available.
    uint64_t min_pages_to_free = 0;
    EvictionLevel level = EvictionLevel::OnlyOldest;
    bool print_counts = false;
  };

  struct EvictedPageCounts {
    uint64_t pager_backed = 0;
    uint64_t discardable = 0;
  };

  explicit Evictor(PmmNode *node);
  Evictor() = delete;
  ~Evictor();

  // Called from the scanner with kernel cmdline values.
  void SetDiscardableEvictionsPercent(uint32_t discardable_percent);
  void SetContinuousEvictionInterval(zx_time_t eviction_interval);
  // Called from the scanner to enable eviction if required. Creates an eviction thread to process
  // asynchronous eviction requests (both one-shot and continuous).
  void EnableEviction();
  // Called from the scanner to disable all eviction if needed, will shut down any in existing
  // eviction thread. It is a responsibility of the scanner to not have multiple concurrent calls
  // to this and EnableEviction.
  void DisableEviction();

  // Set |one_shot_eviction_target_| to the specified |target|. The previous values are overridden.
  void SetOneShotEvictionTarget(EvictionTarget target);

  // Combine the specified |target| with the pre-existing |one_shot_eviction_target_|.
  void CombineOneShotEvictionTarget(EvictionTarget target);

  // Perform a one-shot eviction based on the current values of |one_shot_eviction_target_|. The
  // expectation is that the user will have set the target before calling this function with either
  // SetOneShotEvictionTarget() or CombineOneShotEvictionTarget(). This may acquire arbitrary vmo
  // and aspace locks.
  EvictedPageCounts EvictOneShotFromPreloadedTarget();

  // Performs a synchronous request to evict |min_mem_to_free| (in bytes). The return value is the
  // number of pages evicted. The |eviction_level| is a rough control that maps to how old a page
  // needs to be for being considered for eviction. This may acquire arbitrary vmo and aspace locks.
  uint64_t EvictOneShotSynchronous(uint64_t min_mem_to_free,
                                   EvictionLevel eviction_level = EvictionLevel::OnlyOldest,
                                   Output output = Output::NoPrint);

  // Reclaim memory until free memory equals the |free_mem_target| (in bytes) and at least
  // |min_mem_to_free| (in bytes) has been reclaimed. Reclamation will happen asynchronously on the
  // eviction thread and this function returns immediately. Once the target is reached, or there is
  // no more memory that can be reclaimed, this process will stop and the free memory target will be
  // cleared. The |eviction_level| is a rough control on how hard to try and evict. Multiple calls
  // to EvictOneShotAsynchronous will cause all the targets to get merged by adding together
  // |min_mem_to_free|, taking the max of |free_mem_target| and the highest or most aggressive of
  // any |eviction_level|.
  void EvictOneShotAsynchronous(uint64_t min_mem_to_free, uint64_t free_mem_target,
                                EvictionLevel eviction_level = EvictionLevel::OnlyOldest,
                                Output output = Output::NoPrint);

  // Enable continuous eviction on the eviction thread. Pages are evicted until the free memory
  // level is restored to |free_mem_target| (in bytes) and at least |min_mem_to_free| (in bytes) has
  // been evicted. The eviction thread will re-evaluate these two conditions at a fixed cadence of
  // |default_eviction_interval_| (controlled by the kernel cmdline option
  // `kernel.page-scanner.eviction-interval-seconds`), and continue to evict pages if required,
  // until eviction is explicitly disabled with DisableContinuousEviction(). The |eviction_level| is
  // a rough control that maps to how old a page needs to be for being considered for eviction. The
  // |output| controls whether the eviction thread prints its progress each time it frees pages.
  void EnableContinuousEviction(uint64_t min_mem_to_free, uint64_t free_mem_target,
                                EvictionLevel eviction_level = EvictionLevel::OnlyOldest,
                                Output output = Output::NoPrint);

  // Disable continuous eviction on the eviction thread. Use EnableContinuousEviction() to re-enable
  // eviction when required.
  void DisableContinuousEviction();

  // Whether any eviction (one-shot and continuous) can occur.
  bool IsEvictionEnabled() const;

 private:
  // Private constructor for test code to specify |queues| not owned by |node|.
  Evictor(PmmNode *node, PageQueues *queues);

  // Helpers for testing.
  EvictionTarget DebugGetOneShotEvictionTarget() const;
  void DebugSetMinDiscardableAge(zx_time_t age);

  friend class vm_unittest::TestPmmNode;

  // Evict until |min_pages_to_evict| have been evicted and there are at least |free_pages_target|
  // free pages on the system. Note that the eviction operation here is one-shot, i.e. as soon as
  // the targets are met, eviction will stop and the function will return. Returns the number of
  // discardable and pager-backed pages evicted. This may acquire arbitrary vmo and aspace locks.
  EvictedPageCounts EvictUntilTargetsMet(uint64_t min_pages_to_evict, uint64_t free_pages_target,
                                         EvictionLevel level) TA_EXCL(lock_);

  // Evict the requested number of |target_pages| from discardable vmos. The return value is the
  // number of pages evicted. This may acquire arbitrary vmo and aspace locks.
  uint64_t EvictDiscardable(uint64_t target_pages) const TA_EXCL(lock_);

  // Evict the requested number of |target_pages| from pager-backed vmos. The return value is the
  // number of pages evicted. The |eviction_level| is a rough control that maps to how old a page
  // needs to be for being considered for eviction. This may acquire arbitrary vmo and aspace locks.
  uint64_t EvictPagerBacked(uint64_t target_pages, EvictionLevel eviction_level) const
      TA_EXCL(lock_);

  // The main loop for the eviction thread.
  int EvictionThreadLoop() TA_EXCL(lock_);

  // Control parameters for continuous eviction.
  EvictionTarget continuous_eviction_target_ TA_GUARDED(lock_) = {};
  zx_time_t next_eviction_interval_ TA_GUARDED(lock_) = ZX_TIME_INFINITE;

  // Targets for one-shot eviction, kept separate from the continuous eviction control parameters
  // above.
  EvictionTarget one_shot_eviction_target_ TA_GUARDED(lock_) = {};

  // Event that enforces only one eviction attempt to be active at any time. This prevents us from
  // overshooting the free memory targets required by various simultaneous eviction requests.
  AutounsignalEvent no_ongoing_eviction_{true};

  mutable DECLARE_SPINLOCK(Evictor) lock_;

  // The eviction thread used to process asynchronous requests (both one-shot and continuous).
  // Created only if eviction is enabled i.e. |eviction_enabled_| is set to true.
  Thread *eviction_thread_ = nullptr;
  ktl::atomic<bool> eviction_thread_exiting_ = false;

  // Used by the eviction thread to wait for eviction requests.
  AutounsignalEvent eviction_signal_;

  // The PmmNode whose free level the Evictor monitors, and frees pages to.
  PmmNode *const pmm_node_;

  // The set of PageQueues that the Evictor evicts pages from.
  //
  // This is technically not needed and is mostly for the benefit of unit tests. The Evictor can
  // just call pmm_node_->GetPageQueues() to get the right set of page queues to work on. However,
  // the VMO side code is currently PmmNode agnostic, and until there exists a way for VMOs to
  // allocate from (and free to) a particular PmmNode, we'll need to track the PageQueues separately
  // in order to write meaningful tests.
  //
  // This is set to pmm_node_->GetPageQueues() by the public constructor that passes in the PmmNode
  // associated with this evictor. The private constructor which also passes in PageQueues (not
  // necessarily owned by the PmmNode) is only used in test code.
  PageQueues *const page_queues_;

  // These parameters are initialized later from kernel cmdline options.
  // Whether eviction is enabled.
  bool eviction_enabled_ TA_GUARDED(lock_) = false;
  // A rough percentage of page evictions that should be satisfied from discardable vmos (as opposed
  // to pager-backed vmos). Will require tuning when discardable vmos start being used. Currently
  // sets the number of discardable pages to evict to 0, putting all the burden of eviction on
  // pager-backed pages.
  uint32_t discardable_evictions_percent_ TA_GUARDED(lock_) = 0;
  // The minimum interval a discardable VMO has to be unlocked for to be considered for eviction.
  zx_time_t min_discardable_age_ TA_GUARDED(lock_) = ZX_SEC(10);
  // Default continuous eviction interval. Set to 10s to match the scanner aging interval, since we
  // won't find any new pages to evict before the next aging round.
  zx_time_t default_eviction_interval_ TA_GUARDED(lock_) = ZX_SEC(10);
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_EVICTOR_H_
