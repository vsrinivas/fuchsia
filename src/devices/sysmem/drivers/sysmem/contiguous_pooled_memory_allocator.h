// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_CONTIGUOUS_POOLED_MEMORY_ALLOCATOR_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_CONTIGUOUS_POOLED_MEMORY_ALLOCATOR_H_

#include <lib/async/wait.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/bti.h>
#include <lib/zx/event.h>
#include <zircon/errors.h>
#include <zircon/limits.h>

#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

#include "allocator.h"
#include "protected_ranges.h"

namespace sysmem_driver {

class ContiguousPooledMemoryAllocator : public MemoryAllocator {
 public:
  ContiguousPooledMemoryAllocator(Owner* parent_device, const char* allocation_name,
                                  inspect::Node* parent_node, uint64_t pool_id, uint64_t size,
                                  bool is_always_cpu_accessible, bool is_ever_cpu_accessible,
                                  bool is_ready, bool can_be_torn_down,
                                  async_dispatcher_t* dispatcher);

  ~ContiguousPooledMemoryAllocator();

  // Alignment gets rounded up to system page alignment, so any low number will default to system
  // page alignment.
  zx_status_t Init(uint32_t alignment_log2 = 0);

  // Initializes the guard regions. Must be called after Init. If
  // internal_guard_regions is not set, there will be only guard regions at the
  // begin and end of the buffer.
  void InitGuardRegion(size_t guard_region_size, bool unused_pages_guarded,
                       zx::duration unused_page_check_cycle_period, bool internal_guard_regions,
                       bool crash_on_guard_failure, async_dispatcher_t* dispatcher);
  void FillUnusedRangeWithGuard(uint64_t start_offset, uint64_t size);

  // If is_cpu_accessibe_, called after InitGuardRegion() (if any), but during the same dispatcher
  // call-out, before returning to the dispatcher, because is_ready_ is already true.
  //
  // If !is_cpu_accessibe_, called during set_ready().
  void SetupUnusedPages();

  // This uses a physical VMO as the parent VMO.  This is used for VDEC as we learn the physical
  // range of VDEC from the TEE.
  zx_status_t InitPhysical(zx_paddr_t paddr);

  zx_status_t Allocate(uint64_t size, std::optional<std::string> name,
                       zx::vmo* parent_vmo) override;
  zx_status_t SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo,
                            fuchsia_sysmem2::wire::SingleBufferSettings buffer_settings) override;
  void Delete(zx::vmo parent_vmo) override;
  bool is_empty() override {
    // If the contiguous VMO has been marked as secure there's no way to unmark it as secure, so
    // unbinding would never be safe.
    return regions_.empty() && (can_be_torn_down_ || !is_ready_);
  }

  zx_status_t GetPhysicalMemoryInfo(uint64_t* base, uint64_t* size) override {
    *base = phys_start_;
    *size = size_;
    return ZX_OK;
  }

  void set_ready() override;
  bool is_ready() override;

  const zx::vmo& GetPoolVmoForTest() { return contiguous_vmo_; }
  // Gets the offset of a VMO from the beginning of a pool.
  uint64_t GetVmoRegionOffsetForTest(const zx::vmo& vmo);

  uint64_t failed_guard_region_checks() const { return failed_guard_region_checks_; }

  bool is_already_cleared_on_allocate() override;

  // When this is set from unit tests only, we skip any operation that's only allowed on contiguous
  // VMOs, since we don't have a real contiguous VMO, since a fake BTI can't be used to create one.
  // This ends up limiting the fidelity of the unit tests somewhat; in the long run we probably
  // should plumb a real BTI to the unit tests somehow.
  void SetBtiFakeForUnitTests() {
    ZX_ASSERT(!is_ready());
    is_bti_fake_ = true;
  }
  bool is_bti_fake() { return is_bti_fake_; }

  fuchsia_sysmem2::wire::HeapType heap_type() {
    return static_cast<fuchsia_sysmem2::wire::HeapType>(pool_id_);
  }

  // loanable pages / un-used pages
  //
  // We count pages we choose to pattern instead of loan as loanable, despite not actually loaning
  // those pages.  In other words we don't count patterned pages against efficiency.
  double GetLoanableEfficiency();

  // loanable pages / total pages
  //
  // We count pages we choose to pattern instead of loan as loanable, despite not actually loaning
  // those pages.  In other words we don't count patterned pages against the loaned ratio.
  double GetLoanableRatio();

  // loanable bytes
  //
  // We count pages we choose to pattern instead of loan as loanable, despite not actually loaning
  // those pages.  In other words we don't count patterned pages against the loaned ratio.
  uint64_t GetLoanableBytes();

  static constexpr zx::duration kDefaultUnusedPageCheckCyclePeriod = zx::sec(600);

  static constexpr zx::duration kUnusedRecentlyPageCheckPeriod = zx::sec(2);
  static constexpr zx::duration kUnusedRecentlyAgeThreshold = zx::sec(5);

  static constexpr zx::duration kStepTowardOptimalProtectedRangesPeriod = zx::msec(1000);

  // Keep < 1% of pages aside for being unused page guard pattern.  The rest get loaned back to
  // Zircon.
  static constexpr uint64_t kUnusedGuardPatternPeriodPages = 128;

 private:
  struct RegionData {
    std::string name;
    zx_koid_t koid;
    inspect::Node node;
    inspect::UintProperty size_property;
    inspect::UintProperty koid_property;
    RegionAllocator::Region::UPtr ptr;
  };

  struct DeletedRegion {
    ralloc_region_t region;
    zx::time when_freed;
    std::string name;
  };

  class RangesControl : public protected_ranges::ProtectedRangesControl {
   public:
    RangesControl(ContiguousPooledMemoryAllocator* parent) : parent_(parent) {}

    // protected_ranges::ProtectedRangesControl implementation
    bool IsDynamic() override;
    uint64_t MaxRangeCount() override;
    uint64_t GetRangeGranularity() override;
    bool HasModProtectedRange() override;
    void AddProtectedRange(const protected_ranges::Range& range) override;
    void DelProtectedRange(const protected_ranges::Range& range) override;
    void ModProtectedRange(const protected_ranges::Range& old_range,
                           const protected_ranges::Range& new_range) override;
    void ZeroProtectedSubRange(bool is_covering_range_explicit,
                               const protected_ranges::Range& range) override;
    uint64_t GetBase() override;
    uint64_t GetSize() override;
    bool UseRange(const protected_ranges::Range& range) override;
    void UnUseRange(const protected_ranges::Range& range) override;

   private:
    ContiguousPooledMemoryAllocator* parent_{};
  };

  zx_status_t InitCommon(zx::vmo local_contiguous_vmo);
  void TraceObserverCallback(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                             zx_status_t status, const zx_packet_signal_t* signal);

  void CheckGuardPageCallback(async_dispatcher_t* dispatcher, async::TaskBase* task,
                              zx_status_t status);
  void CheckUnusedPagesCallback(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                zx_status_t status);
  void CheckUnusedRecentlyPagesCallback(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                        zx_status_t status);
  void CheckGuardRegion(const char* region_name, size_t region_size, bool pre,
                        uint64_t start_offset);
  void IncrementGuardRegionFailureInspectData();
  void CheckGuardRegionData(const RegionData& region);
  void CheckExternalGuardRegions();
  void CheckAnyUnusedPages(uint64_t start_offset, uint64_t end_offset);
  void CheckUnusedRange(uint64_t offset, uint64_t size, bool and_also_zero);
  void DumpPoolStats();
  void DumpPoolHighWaterMark();
  void TracePoolSize(bool initial_trace);
  uint64_t CalculateLargeContiguousRegionSize();
  void UpdateLoanableMetrics();

  // This method iterates over all the sub-regions of an unused region.  The sub-regions are regions
  // we need to pattern and keep, loan to zircon, or zero.  Any given page that's unused will always
  // (in any given boot) be pattern, loan, or zero, regardless of the alignment of the unused
  // region.  This way we'll know which pages are supposed to be patterned, loaned, or zeroed
  // despite unused regions getting merged/split.
  //
  // Depending on settings, some sub-region types won't exist, so their corresponding callable won't
  // be called.
  //
  // The pattern_func, loan_func, and zero_func take different actions depending on calling context,
  // but generally each func is supposed to handle the pages that are supposed to be patterned,
  // loaned, or zeroed.  For example, write the pattern or check the pattern, loan the page or
  // un-loan the page, zero the page or nop.
  //
  // If a page is protected it'll be skipped, and not processed by any of the passed-in funcs.
  //
  // All the funcs take const ralloc_region_t&.
  template <typename F1, typename F2, typename F3>
  void ForUnusedGuardPatternRanges(const ralloc_region_t& region, F1 pattern_func, F2 loan_func,
                                   F3 zero_func);

  // This handles the unprotected portions of the region passed to ForUnusedGuardPatternRanges().
  template <typename F1, typename F2, typename F3>
  void ForUnusedGuardPatternRangesInternal(const ralloc_region_t& region, F1 pattern_func,
                                           F2 loan_func, F3 zero_func);

  void StashDeletedRegion(const RegionData& region_data);
  DeletedRegion* FindMostRecentDeletedRegion(uint64_t offset);
  // Log DeletedRegion info and fairly detailed diff info for a range that's detected to differ from
  // the pattern that was previously written.
  //
  // TODO(dustingreen): With some refactoring we could have common code for diff reporting, for all
  // of per-reserved-range guard pages, per-allocation guard pages, and unused page guard pages.
  void ReportPatternCheckFailedRange(const ralloc_region_t& failed_range, const char* which_type);

  void OnRegionUnused(const ralloc_region_t& region);
  zx_status_t CommitRegion(const ralloc_region_t& region);

  void EnsureSteppingTowardOptimalProtectedRanges();
  void StepTowardOptimalProtectedRanges(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                        zx_status_t status);

  void DumpRanges() const;

  Owner* const parent_device_{};
  async_dispatcher_t* dispatcher_{};
  const char* const allocation_name_{};
  const uint64_t pool_id_{};
  char child_name_[ZX_MAX_NAME_LEN] = {};

  uint64_t guard_region_size_ = 0;
  // Holds the default data to be placed into the guard region.
  std::vector<uint8_t> guard_region_data_;
  // Holds a copy of the guard region data that's compared with the real value.
  std::vector<uint8_t> guard_region_copy_;

  bool crash_on_guard_failure_ = false;
  // Internal guard regions are around every allocation, and not just the beginning and end of the
  // contiguous VMO.
  bool has_internal_guard_regions_ = false;

  zx::vmo contiguous_vmo_;
  zx::pmt pool_pmt_;
  RegionAllocator region_allocator_;
  uint64_t allocated_bytes_ = 0;

  // We run protected_ranges_ in the same [0, size_) space as region_allocator_, and convert to
  // physical ranges in protected_ranges_control_ (adding phys_start_).
  std::optional<protected_ranges::ProtectedRanges> protected_ranges_;
  // When allocating/deallocating a buffer, we immediately make the necessary/possible changes via
  // protection_ranges_ to make that buffer space usable/best-effort-reclaimable, but to really
  // optimize the protection ranges we need to spread out the changes in time to avoid churning all
  // the loaned pages at once.  This timer does that.
  async::TaskMethod<ContiguousPooledMemoryAllocator,
                    &ContiguousPooledMemoryAllocator::StepTowardOptimalProtectedRanges>
      step_toward_optimal_protected_ranges_{this};
  // We effectively reset the timer any time there's new allocate/deallocate activity, since that
  // activity is also churn in some sense, so we avoid compounding that churn with optimizing
  // steps until more time has passed, even if the timer had previously been set to go off soon.
  zx::time step_toward_optimal_protected_ranges_min_time_ = zx::time::infinite_past();
  // The bottom edge of protected_ranges_ uses protected_ranges_control_ to effect actual changes.
  // This delegates to ContiguousPooledMemoryAllocator or Device (and then SecureMem) to do the
  // changes.
  std::optional<RangesControl> protected_ranges_control_;

  // From parent_vmo handle to std::unique_ptr<>
  std::map<zx_handle_t, RegionData> regions_;
  zx_paddr_t phys_start_{};
  uint64_t size_{};
  // True if the CPU can always touch these pages.  False if these pages are under a HW protected
  // range at least sometimes.
  bool is_always_cpu_accessible_{};
  // True if the CPU can sometimes touch these pages.  False if these pages are under a HW protected
  // range 100% of the time).
  bool is_ever_cpu_accessible_{};
  // True if the VMO is a normal contiguous VMO.  False if the VMO is a physical VMO, which doesn't
  // support decommit (and we don't need it to, since a physical VMO is only use when
  // !is_ever_cpu_accessible_).
  bool can_decommit_{};
  bool is_ready_{};
  // True if the allocator can be deleted after it's marked ready.
  bool can_be_torn_down_{};
  bool is_setup_unused_pages_called_{};

  uint64_t failed_guard_region_checks_{};

  uint64_t high_water_mark_used_size_{};
  uint64_t max_free_size_at_high_water_mark_{};

  inspect::Node node_;
  inspect::ValueList properties_;
  inspect::UintProperty size_property_;
  inspect::UintProperty high_water_mark_property_;
  inspect::UintProperty used_size_property_;
  inspect::UintProperty allocations_failed_property_;
  inspect::UintProperty last_allocation_failed_timestamp_ns_property_;
  inspect::UintProperty commits_failed_property_;
  inspect::UintProperty last_commit_failed_timestamp_ns_property_;
  // Keeps track of how many allocations would have succeeded but failed due to fragmentation.
  inspect::UintProperty allocations_failed_fragmentation_property_;
  // This is the size of a the largest free contiguous region when high_water_mark_property_ was
  // last modified. It can be used to determine how much space was wasted due to fragmentation.
  inspect::UintProperty max_free_at_high_water_property_;
  // size - high_water_mark. This is used for cobalt reporting.
  inspect::UintProperty free_at_high_water_mark_property_;
  inspect::BoolProperty is_ready_property_;
  inspect::UintProperty failed_guard_region_checks_property_;
  inspect::UintProperty last_failed_guard_region_check_timestamp_ns_property_;
  // This tracks the sum of the size of the 10 largest free regions.
  inspect::UintProperty large_contiguous_region_sum_property_;

  // CMM / PCMM properties regarding loaning of pages to Zircon.
  //
  // The minimum efficiency since this class was created.
  double min_efficiency_ = 1.0;
  inspect::DoubleProperty loanable_efficiency_property_;
  inspect::DoubleProperty loanable_ratio_property_;
  inspect::UintProperty loanable_bytes_property_;
  inspect::UintProperty loanable_mebibytes_property_;

  zx::event trace_observer_event_;
  async::WaitMethod<ContiguousPooledMemoryAllocator,
                    &ContiguousPooledMemoryAllocator::TraceObserverCallback>
      wait_{this};

  async::TaskMethod<ContiguousPooledMemoryAllocator,
                    &ContiguousPooledMemoryAllocator::CheckGuardPageCallback>
      guard_checker_{this};

  // Split up the unused page check into relatively small pieces to avoid spiking the CPU or
  // causing latency spikes for normal sysmem requests.
  static constexpr uint32_t kUnusedCheckPartialCount = 64;
  // We do this one page at a time to hopefully stay within L1 on all devices, since in the allocate
  // path we're checking this amount of buffer space with memcmp(), then also zeroing the same space
  // with memset().  If we did so in chunks larger than L1, we'd be spilling cache lines to L2
  // or RAM during memcmp(), then pulling them back in during memset().  Cache sizes and tiers can
  // vary of course.  This also determines the granularity at which we report pattern mismatch
  // failures, so 1 page is best here for that also.
  const uint64_t unused_guard_data_size_ = zx_system_get_page_size();
  bool unused_pages_guarded_ = false;
  zx::duration unused_page_check_cycle_period_ = kDefaultUnusedPageCheckCyclePeriod;
  uint64_t unused_check_phase_ = 0;
  async::TaskMethod<ContiguousPooledMemoryAllocator,
                    &ContiguousPooledMemoryAllocator::CheckUnusedPagesCallback>
      unused_checker_{this};
  async::TaskMethod<ContiguousPooledMemoryAllocator,
                    &ContiguousPooledMemoryAllocator::CheckUnusedRecentlyPagesCallback>
      unused_recently_checker_{this};
  SysmemMetrics& metrics_;

  // Regardless of is_ever_cpu_accessible_, we create a mapping of the whole vmo.  When
  // is_always_cpu_accessible_ we can use the mapping to zero new buffers.  When
  // is_ever_cpu_accessible_ we can use the mapping to write and check patterns in unused pages.
  uint8_t* mapping_ = nullptr;

  // While we'll typically pattern only 1 page per pattern period and adjust the pattern period to
  // get the % we want, being able to vary this might potentially help catch a suspected problem
  // faster; in any case it's simple enough to allow this to be adjusted.
  static constexpr uint64_t kUnusedToPatternPages = 1;
  const uint64_t unused_guard_pattern_period_bytes_ =
      kUnusedGuardPatternPeriodPages * zx_system_get_page_size();
  const uint64_t unused_to_pattern_bytes_ = kUnusedToPatternPages * zx_system_get_page_size();

  bool is_bti_fake_ = false;

  // We cap the number of DeletedRegion we're willing to track; otherwise the overhead could get a
  // bit excessive in pathological cases if we were to allow tracking a DeletedRegion per page for
  // example.  This is optimized for update, not (at all) for lookup, since we only do lookups if
  // a page just failed a pattern check, which should never happen.  If it does happen, we want to
  // know the paddr_t range and name of the most-recently-deleted region, and possibly the 2nd most
  // recently deleted region also, if it comes to that.
  static constexpr int32_t kNumDeletedRegions = 512;
  int32_t deleted_regions_count_ = 0;
  int32_t deleted_regions_next_ = 0;
  // Only allocate if we'll be checking unused pages.
  std::vector<DeletedRegion> deleted_regions_;

  // This is Zircon's zero page mapped a few times, read-only.
  uint64_t zero_page_vmo_size_ = fbl::round_up(64ull * 1024, zx_system_get_page_size());
  zx::vmo zero_page_vmo_;
  uint8_t* zero_page_vmo_base_ = nullptr;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_CONTIGUOUS_POOLED_MEMORY_ALLOCATOR_H_
