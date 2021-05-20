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
    // The desired value to get pmm_count_free_pages() to
    uint64_t free_pages_target = 0;
    // A minimum amount of pages we want to evict, regardless of how much free memory is available.
    uint64_t min_pages_to_free = 0;
    EvictionLevel level = EvictionLevel::OnlyOldest;
  };

  struct EvictedPageCounts {
    uint64_t pager_backed = 0;
    uint64_t discardable = 0;
  };

  Evictor() = default;
  ~Evictor() = default;

  // Called from the scanner with kernel cmdline values.
  void SetEvictionEnabled(bool eviction_enabled);
  void SetDiscardableEvictionsPercent(uint32_t discardable_percent);

  // Set |one_shot_eviction_target_| to the specified |target|. The previous values are overridden.
  void SetOneShotEvictionTarget(EvictionTarget target);

  // Combine the specified |target| with the pre-existing |one_shot_eviction_target_|.
  void CombineOneShotEvictionTarget(EvictionTarget target);

  // Perform a one-shot eviction based on the current values of |one_shot_eviction_target_|. The
  // expectation is that the user will have set the target before calling this function with either
  // SetOneShotEvictionTarget() or CombineOneShotEvictionTarget(). This may acquire arbitrary vmo
  // and aspace locks.
  EvictedPageCounts EvictOneShotFromPreloadedTarget();

  // Performs a synchronous request to evict the requested amount of memory (in bytes). The return
  // value is the number of pages evicted. The |eviction_level| is a rough control that maps to how
  // old a page needs to be for being considered for eviction. This may acquire arbitrary vmo and
  // aspace locks.
  uint64_t EvictOneShotSynchronous(uint64_t min_mem_to_free, EvictionLevel eviction_level,
                                   Output output);

  // Whether any eviction (one-shot and continuous) can occur.
  bool IsEvictionEnabled() const;

 private:
  // Evict until |min_pages_to_evict| have been evicted and there are at least |free_pages_target|
  // free pages on the system. Note that the eviction operation here is one-shot, i.e. as soon as
  // the targets are met, eviction will stop and the function will return. Returns the number of
  // discardable and pager-backed pages evicted. This may acquire arbitrary vmo and aspace locks.
  EvictedPageCounts EvictUntilTargetsMet(uint64_t min_pages_to_evict, uint64_t free_pages_target,
                                         EvictionLevel level) TA_EXCL(lock_);

  // Evict the requested number of |target_pages| from discardable vmos. The return value is the
  // number of pages evicted. This may acquire arbitrary vmo and aspace locks.
  uint64_t EvictDiscardable(uint64_t target_pages) const TA_EXCL(lock_);

  // Evict the requested number of |target_pages| from pager-backed vmos. Evicted pages are placed
  // in the passed |free_list| and become owned by the caller, with the return value being the
  // number of free pages. The |eviction_level| is a rough control that maps to how old a page needs
  // to be for being considered for eviction. This may acquire arbitrary vmo and aspace locks.
  uint64_t EvictPagerBacked(uint64_t target_pages, EvictionLevel eviction_level,
                            list_node_t* free_list) const TA_EXCL(lock_);

  // Targets for one-shot eviction.
  EvictionTarget one_shot_eviction_target_ TA_GUARDED(lock_) = {};

  // Event that enforces only one eviction attempt to be active at any time. This prevents us from
  // overshooting the free memory targets required by various simultaneous eviction requests.
  AutounsignalEvent no_ongoing_eviction_{true};

  mutable DECLARE_SPINLOCK(Evictor) lock_;

  // These parameters are initialized later from kernel cmdline options.
  // Whether eviction is enabled.
  bool eviction_enabled_ TA_GUARDED(lock_) = true;
  // A rough percentage of page evictions that should be satisfied from discardable vmos (as opposed
  // to pager-backed vmos). Will require tuning when discardable vmos start being used. Currently
  // sets the number of discardable pages to evict to 0, putting all the burden of eviction on
  // pager-backed pages.
  uint32_t discardable_evictions_percent_ TA_GUARDED(lock_) = 0;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_EVICTOR_H_
