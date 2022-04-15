// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "protected_ranges.h"

#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>

#include <limits>
#include <random>

#include <zxtest/zxtest.h>

// Output the same way as protected_ranges.cc so we won't get shifted output.
#define LOG(severity, fmt, ...) zxlogf(severity, fmt, ##__VA_ARGS__)

#define DLOG_ENABLED 1

#if DLOG_ENABLED
#define DLOG(fmt, ...) LOG(INFO, "  test output: " fmt, ##__VA_ARGS__)
#else
#define DLOG(fmt, ...)
#endif

namespace {

using Range = protected_ranges::Range;
using Ranges = protected_ranges::ProtectedRanges::Ranges;

void CheckNoOverlap(const Ranges& ranges) {
  std::optional<uint64_t> prev_end;
  for (auto& range : ranges) {
    auto set_prev_end = fit::defer([&prev_end, &range] { prev_end = {range.end()}; });
    if (!prev_end) {
      continue;
    }
    ZX_ASSERT(range.begin() >= prev_end);
    ZX_ASSERT(range.length() != 0);
  }
}

void CheckNoOverlapAndCoalesced(const Ranges& ranges) {
  std::optional<uint64_t> prev_end;
  for (auto& range : ranges) {
    auto set_prev_end = fit::defer([&prev_end, &range] { prev_end = {range.end()}; });
    if (!prev_end) {
      continue;
    }
    ZX_ASSERT(range.begin() > prev_end);
    ZX_ASSERT(range.length() != 0);
  }
}

}  // namespace

class ProtectedRangesTest : public zxtest::Test, public protected_ranges::ProtectedRangesControl {
 public:
  class TestRange {
   public:
    static TestRange Empty() { return TestRange{0, 0}; }
    static TestRange BeginLength(uint64_t begin, uint64_t length) {
      return TestRange{begin, length};
    }
    static TestRange BeginEnd(uint64_t begin, uint64_t end) {
      return TestRange{begin, end - begin};
    }
    uint64_t begin() const { return begin_; }
    uint64_t end() const { return begin_ + length_; }
    uint64_t length() const { return length_; }
    bool empty() const {
      ZX_ASSERT(end() >= begin());
      return length_ == 0;
    }
    bool operator<(const TestRange& rhs) const {
      if (begin_ < rhs.begin_) {
        // <
        return true;
      }
      if (begin_ > rhs.begin_) {
        // >
        return false;
      }

      if (length_ < rhs.length_) {
        // <
        return true;
      }
      if (length_ > rhs.length_) {
        // >
        return false;
      }

      // ==
      return false;
    }

   private:
    TestRange(uint64_t begin, uint64_t length) : begin_(begin), length_(length) {}
    uint64_t begin_;
    uint64_t length_;
  };

  void SetUp() override {
    upper_bitmap_.resize(paddr_size_, false);
    lower_refcounts_.resize(paddr_size_, 0);
    lower_used_bitmap_.resize(paddr_size_, false);
    protected_ranges_.emplace(this);
    CheckInterOpInvariants();
  }

  void TearDown() override {
    CheckInterOpInvariants();
    CheckLeaks();
    protected_ranges_.reset();
  }

  void SetMaxRangeCount(uint64_t max_range_count) {
    ZX_ASSERT(max_range_count >= 1);
    max_range_count_ = max_range_count;
  }

  void CheckInterOpInvariants();
  void CheckIntraOpInvariants();
  void CheckLeaks();
  void CheckAligned(const Ranges& ranges);

  void AddRandomRange();
  void RemoveRandomRange();

  TestRange ConvertRangeFrom(const protected_ranges::Range& range) {
    return TestRange::BeginLength(range.begin() - paddr_begin_, range.length());
  }

  protected_ranges::Range ConvertRangeFrom(const TestRange& range) {
    return Range::BeginLength(range.begin() + paddr_begin_, range.length());
  }

  // ProtectedRangesControl interface
  bool IsDynamic() override;
  uint64_t MaxRangeCount() override;
  uint64_t GetRangeGranularity() override;
  bool HasModProtectedRange() override;
  uint64_t GetBase() override;
  uint64_t GetSize() override;
  void AddProtectedRange(const Range& range) override;
  void DelProtectedRange(const Range& range) override;
  void ModProtectedRange(const Range& old_range, const Range& new_range) override;
  void ZeroProtectedSubRange(bool is_covering_range_explicit, const Range& range) override;
  bool UseRange(const Range& range) override;
  void UnUseRange(const Range& range) override;

 protected:
  void AddProtectedRangeInternal(const Range& range);
  void DelProtectedRangeInternal(const Range& range);

  void CheckRangeAdd(const TestRange& range);
  void CheckRangeDel(const TestRange& range);
  void CheckRangeMod(const TestRange& old_range, const TestRange& new_range);

  void Add(const TestRange& range);
  void Remove(const TestRange& range);

  void FlushIncrementalOptimization();

  bool TestRangesOverlap(const TestRange& a, const TestRange& b);

  std::optional<protected_ranges::ProtectedRanges> protected_ranges_;

  // This limits ranges out the bottom of ProtectedRanges, not ranges in the top.
  uint64_t max_range_count_ = 9;
  // Must be a power of 2.  The default of 4 should cover all the interesting cases without being
  // much bigger than necessary to do that.
  uint64_t range_granularity_ = 4;
  // The default of true is how we run on real devices (at least for now).
  bool has_mod_protected_range_ = true;

  // Fits in the width of a plausible text window for easier debugging.
  uint64_t paddr_size_ = 240;
  // Aligned only as well as range_granularity_, but no better.
  uint64_t paddr_begin_ = 127 * range_granularity_;

  uint64_t max_upper_range_length_ = paddr_size_ / (max_range_count_ / 4);

  // Ranges we've fed into ProtectedRanges's AddRange() and DeleteRange(), into the "top" of
  // ProtectedRanges.
  std::set<TestRange> upper_ranges_;
  std::vector<bool> upper_bitmap_;
  uint32_t upper_used_ = 0;

  // Ranges controlled via ProtectedRangesControl, out the "bottom" of ProtectedRanges, via
  // AddProtectedRange(), DelProtectedRange(), and ModProtectedRange().
  //
  // lower_ranges_ can have overlap, but won't have exact duplicates.
  std::set<TestRange> lower_ranges_;
  // Counts how many ranges overlap each block.
  std::vector<uint64_t> lower_refcounts_;
  // Ranges controlled via ProtectedRangesControl, out the "bottom" of ProtectedRanges, via
  // UseRange() and UnUseRange().  The blocks covered by a UseRange() will all be switching from
  // false to true, and the blocks covered by an UnUseRange() will all be switching from true to
  // false, but UnUseRange() calls are not required to refer to ranges previously added using
  // UseRange(); UnUseRange() need only be referring to blocks that are currently used (true).
  std::vector<bool> lower_used_bitmap_;

  // For generating different sequences of random ranges, but still being able to easily repro any
  // failure by putting the uint64_t seed inside the {} here.
  static constexpr std::optional<uint64_t> kForcedSeed{};
  std::random_device random_device_;
  std::uint_fast64_t seed_{kForcedSeed ? *kForcedSeed : random_device_()};
  std::mt19937_64 prng_{seed_};
};

void ProtectedRangesTest::CheckInterOpInvariants() {
  CheckIntraOpInvariants();

  // Every offset of each upper range must be covered by at least one lower range.
  for (auto& upper_range : upper_ranges_) {
    for (uint64_t i = upper_range.begin(); i < upper_range.end(); ++i) {
      ZX_ASSERT(lower_refcounts_[i] >= 1);
    }
  }

  // In addition to the intra-op invariants, if we're between upper ops we can assert that we've
  // oportunistically coalesced ranges in ranges_, so we don't have any touching ranges in ranges_
  // between upper ops.
  //
  // Touching is defined as immediately adjacent with no overlap and no gap in between, or
  // overlapping.
  std::optional<uint64_t> prev_end;
  for (auto& lower_range : lower_ranges_) {
    ZX_ASSERT(!prev_end || lower_range.begin() > *prev_end);
    prev_end = {lower_range.end()};
  }

  CheckNoOverlap(protected_ranges_->requested_ranges());
  // required_ranges() is allowed to have overlap
  CheckNoOverlapAndCoalesced(protected_ranges_->coalesced_required_ranges());
  CheckNoOverlapAndCoalesced(protected_ranges_->interior_unused_ranges());
  CheckNoOverlapAndCoalesced(protected_ranges_->largest_interior_unused_ranges());
  CheckNoOverlapAndCoalesced(protected_ranges_->goal_ranges());
  CheckNoOverlapAndCoalesced(protected_ranges_->ranges());

  CheckAligned(protected_ranges_->required_ranges());
  CheckAligned(protected_ranges_->coalesced_required_ranges());
  CheckAligned(protected_ranges_->interior_unused_ranges());
  CheckAligned(protected_ranges_->largest_interior_unused_ranges());
  CheckAligned(protected_ranges_->goal_ranges());
  CheckAligned(protected_ranges_->ranges());
}

void ProtectedRangesTest::CheckIntraOpInvariants() {
  ZX_ASSERT(upper_used_ <= paddr_size_);
  ZX_ASSERT(upper_ranges_.size() <= paddr_size_);

  // Check self-consistency of "upper" data.
  uint64_t prev_end = 0;
  uint64_t last_end = 0;
  for (auto& upper_range : upper_ranges_) {
    if (upper_range.begin() > prev_end) {
      for (uint64_t i = prev_end; i < upper_range.begin(); ++i) {
        ZX_ASSERT(!upper_bitmap_[i]);
      }
    }
    for (uint64_t i = upper_range.begin(); i < upper_range.end(); ++i) {
      ZX_ASSERT(upper_bitmap_[i]);
    }
    prev_end = upper_range.end();
    ZX_ASSERT(upper_range.end() > last_end);
    last_end = upper_range.end();
  }
  for (uint64_t i = last_end; i < paddr_size_; ++i) {
    ZX_ASSERT(!upper_bitmap_[i]);
  }

  // Must always stay under max_range_count_.
  ZX_ASSERT(lower_ranges_.size() <= max_range_count_);

  // Every lower_range_ must have only used pages from Zircon's point of view.
  for (auto& lower_range : lower_ranges_) {
    for (uint64_t i = lower_range.begin(); i < lower_range.end(); ++i) {
      // Every offset of every lower range must be "used" in the sense of not being loaned to
      // Zircon, for the entire lifetime of the lower range.
      ZX_ASSERT(lower_used_bitmap_[i]);
    }
    ZX_ASSERT(lower_range.begin() % range_granularity_ == 0);
    ZX_ASSERT(lower_range.end() % range_granularity_ == 0);
  }

  // All begin() and end() in required_ranges_ are required to be range_granularity_ aligned.
  const auto& required_ranges = protected_ranges_->required_ranges();
  for (auto iter = required_ranges.begin(); iter != required_ranges.end(); ++iter) {
    const Range& a = *iter;
    ZX_ASSERT(a.begin() % range_granularity_ == 0);
    ZX_ASSERT(a.length() % range_granularity_ == 0);
  }
  // For required_ranges_, for any items a, b adjacent to each other in sorted order, we know that
  // (a.begin() <= b.begin()) == (a.end() <= b.end()).  Assert this here.
  if (protected_ranges_->required_ranges().size() >= 2) {
    const auto& required_ranges = protected_ranges_->required_ranges();
    auto end = required_ranges.end();
    --end;
    for (auto iter = required_ranges.begin(); iter != end; ++iter) {
      const Range& a = *iter;
      auto b_iter = iter;
      ++b_iter;
      const Range& b = *b_iter;
      // This allows for a restricted degree of overlap, but not ranges that completely "cross" each
      // other.  Anotehr way of saying this is if one were to subtract any range from any other
      // range in the set, the result would only ever be 0 or 1 ranges, never 2.  This is a
      // less-restrictive check than the constraint the actual ranges in required_ranges_ will
      // satisfy.
      ZX_ASSERT((a.begin() <= b.begin()) == (a.end() <= b.end()));
      // In addition, when overlap exists, it is limited to exactly range_granularity_ in size.
      ZX_ASSERT(a.end() <= b.begin() || a.end() - b.begin() == range_granularity_);
    }
  }
}

void ProtectedRangesTest::CheckLeaks() {
  ZX_ASSERT(upper_ranges_.empty());
  for (uint64_t i = 0; i < paddr_size_; ++i) {
    ZX_ASSERT(!upper_bitmap_[i]);
  }
  ZX_ASSERT(lower_ranges_.empty());
  for (uint64_t i = 0; i < paddr_size_; ++i) {
    ZX_ASSERT(lower_refcounts_[i] == 0);
    ZX_ASSERT(!lower_used_bitmap_[i]);
  }
}

void ProtectedRangesTest::CheckAligned(const Ranges& ranges) {
  for (auto& range : ranges) {
    ZX_ASSERT(range.begin() % range_granularity_ == 0);
    ZX_ASSERT(range.length() % range_granularity_ == 0);
  }
}

void ProtectedRangesTest::AddRandomRange() {
  CheckInterOpInvariants();
  // For begin, pick a random offset among the offsets that are not presently used.
  std::uniform_int_distribution<uint64_t> begin_distribution(0, paddr_size_ - upper_used_ - 1);
  uint64_t target_offset_within_unused = begin_distribution(prng_);
  // Find the actual offset that corresponds to the offset_within_unused'th unused offset.
  uint64_t offset_within_unused = 0;
  uint64_t begin = paddr_size_;
  // If paddr_size_ were huge, we could use a rope-like data structure with tracking of original
  // offset as well as the offset within free space, but paddr_size_ isn't huge, and doesn't need
  // to be huge to cover all the relevant cases.
  for (uint64_t i = 0; i < paddr_size_; ++i) {
    if (upper_bitmap_[i]) {
      continue;
    }
    if (offset_within_unused == target_offset_within_unused) {
      begin = i;
      break;
    }
    ++offset_within_unused;
  }
  ZX_ASSERT(begin < paddr_size_);
  auto next_begin_iter = upper_ranges_.lower_bound(TestRange::BeginLength(begin, 0));
  uint64_t last_valid_end = paddr_size_;
  if (next_begin_iter != upper_ranges_.end()) {
    // Even though we used lower_bound() instead of upper_bound(), we still know that the range with
    // begin() >= begin will have begin() > begin because we know there's no range overlapping with
    // begin.
    ZX_ASSERT(begin < next_begin_iter->begin());
    last_valid_end = next_begin_iter->begin();
  }
  if (last_valid_end - begin > max_upper_range_length_) {
    last_valid_end = begin + max_upper_range_length_;
  }
  // For length, we need 1 to the highest possible end which lands at next_begin->begin(); any
  // larger and we'd intersect with the next range.
  std::uniform_int_distribution<uint64_t> length_distribution(1, last_valid_end - begin);
  uint64_t length = length_distribution(prng_);
  const auto random_range = TestRange::BeginLength(begin, length);
  Add(random_range);
  CheckInterOpInvariants();
}

void ProtectedRangesTest::RemoveRandomRange() {
  CheckInterOpInvariants();
  ZX_ASSERT(!upper_ranges_.empty());
  std::uniform_int_distribution<uint32_t> which_used_distribution(0, upper_used_ - 1);
  uint64_t target_which_used = which_used_distribution(prng_);
  uint64_t which_used = 0;
  uint64_t to_remove_offset = paddr_size_;
  for (uint64_t i = 0; i < paddr_size_; ++i) {
    if (!upper_bitmap_[i]) {
      continue;
    }
    if (which_used == target_which_used) {
      to_remove_offset = i;
      break;
    }
    ++which_used;
  }
  ZX_ASSERT(to_remove_offset < paddr_size_);
  auto to_remove_iter = upper_ranges_.upper_bound(
      TestRange::BeginLength(to_remove_offset, std::numeric_limits<uint64_t>::max()));
  ZX_ASSERT(to_remove_iter != upper_ranges_.begin());
  --to_remove_iter;
  TestRange to_remove = *to_remove_iter;
  Remove(to_remove);
  CheckInterOpInvariants();
}

void ProtectedRangesTest::Add(const TestRange& range) {
  CheckInterOpInvariants();
  bool add_result = protected_ranges_->AddRange(ConvertRangeFrom(range));
  if (!add_result) {
    return;
  }
  upper_ranges_.insert(range);
  for (uint64_t i = range.begin(); i != range.end(); ++i) {
    ZX_ASSERT(!upper_bitmap_[i]);
    upper_bitmap_[i] = true;
  }
  upper_used_ += range.length();
  CheckInterOpInvariants();
}

void ProtectedRangesTest::Remove(const TestRange& range) {
  upper_ranges_.erase(range);
  for (uint64_t i = range.begin(); i != range.end(); ++i) {
    ZX_ASSERT(upper_bitmap_[i]);
    upper_bitmap_[i] = false;
  }
  ZX_ASSERT(upper_used_ >= range.length());
  upper_used_ -= range.length();
  CheckInterOpInvariants();
  protected_ranges_->DeleteRange(ConvertRangeFrom(range));
  CheckInterOpInvariants();
}

bool ProtectedRangesTest::IsDynamic() { return true; }

uint64_t ProtectedRangesTest::MaxRangeCount() { return max_range_count_; }

uint64_t ProtectedRangesTest::GetRangeGranularity() { return range_granularity_; }

bool ProtectedRangesTest::HasModProtectedRange() { return has_mod_protected_range_; }

uint64_t ProtectedRangesTest::GetBase() { return paddr_begin_; }

uint64_t ProtectedRangesTest::GetSize() { return paddr_size_; }

void ProtectedRangesTest::AddProtectedRange(const protected_ranges::Range& range) {
  CheckIntraOpInvariants();
  AddProtectedRangeInternal(range);
  CheckIntraOpInvariants();
}

void ProtectedRangesTest::AddProtectedRangeInternal(const protected_ranges::Range& range) {
  TestRange test_range = ConvertRangeFrom(range);
  CheckRangeAdd(test_range);
  lower_ranges_.insert(test_range);
  for (uint64_t i = test_range.begin(); i < test_range.end(); ++i) {
    ++lower_refcounts_[i];
  }
}

void ProtectedRangesTest::DelProtectedRange(const Range& range) {
  CheckIntraOpInvariants();
  DelProtectedRangeInternal(range);
  CheckIntraOpInvariants();
}

void ProtectedRangesTest::DelProtectedRangeInternal(const Range& range) {
  TestRange test_range = ConvertRangeFrom(range);
  CheckRangeDel(test_range);
  size_t erase_result = lower_ranges_.erase(test_range);
  ZX_ASSERT(erase_result == 1);
  for (uint64_t i = test_range.begin(); i != test_range.end(); ++i) {
    --lower_refcounts_[i];
  }
}

void ProtectedRangesTest::ModProtectedRange(const Range& old_range, const Range& new_range) {
  CheckIntraOpInvariants();

  TestRange test_old = ConvertRangeFrom(old_range);
  TestRange test_new = ConvertRangeFrom(new_range);
  CheckRangeMod(test_old, test_new);

  // We add before we delete because the logical delete can only disrupt ongoing DMA if there's any
  // zeroing needed, and zeroing is not needed for the portion that overlaps with the "after" range.
  AddProtectedRangeInternal(new_range);
  // We intentionally don't verify the lower range count at this point, because the add/del here is
  // a test implementation detail.  This add/del would not be performed on real HW supporting a real
  // ModProtectedRange().
  DelProtectedRangeInternal(old_range);

  CheckIntraOpInvariants();
}

void ProtectedRangesTest::ZeroProtectedSubRange(bool is_covering_range_explicit,
                                                const protected_ranges::Range& range) {
  CheckIntraOpInvariants();
  TestRange test_range = ConvertRangeFrom(range);
  auto covering_range_lower = lower_ranges_.upper_bound(
      TestRange::BeginLength(test_range.begin(), std::numeric_limits<uint64_t>::max()));
  if (covering_range_lower != lower_ranges_.begin()) {
    --covering_range_lower;
  }
  ZX_ASSERT(covering_range_lower->end() >= test_range.end());
  ZX_ASSERT(covering_range_lower->begin() <= test_range.begin());
  for (auto iter = lower_ranges_.begin(); iter != lower_ranges_.end(); ++iter) {
    if (iter == covering_range_lower) {
      continue;
    }
    if (iter->end() <= test_range.begin()) {
      continue;
    }
    if (iter->begin() >= test_range.end()) {
      continue;
    }
    ZX_ASSERT_MSG(false,
                  "ZeroProtectedSubRange() called on range that overlaps more than one protected "
                  "range (lower)");
  }
  CheckIntraOpInvariants();
}

bool ProtectedRangesTest::UseRange(const protected_ranges::Range& range) {
  CheckIntraOpInvariants();
  std::uniform_int_distribution<uint32_t> sim_fail_distribution(0, 99);
  uint32_t sim_fail_roll = sim_fail_distribution(prng_);
  if (sim_fail_roll < 5) {
    return false;
  }
  const TestRange test_range = ConvertRangeFrom(range);
  for (uint64_t i = test_range.begin(); i < test_range.end(); ++i) {
    ZX_ASSERT(!lower_used_bitmap_[i]);
    lower_used_bitmap_[i] = true;
  }
  CheckIntraOpInvariants();
  return true;
}

void ProtectedRangesTest::UnUseRange(const protected_ranges::Range& range) {
  CheckIntraOpInvariants();
  const TestRange test_range = ConvertRangeFrom(range);
  for (uint64_t i = test_range.begin(); i != test_range.end(); ++i) {
    ZX_ASSERT(lower_used_bitmap_[i]);
    lower_used_bitmap_[i] = false;
  }
  CheckIntraOpInvariants();
}

void ProtectedRangesTest::CheckRangeAdd(const TestRange& range) {
  for (uint64_t i = range.begin(); i != range.end(); ++i) {
    // Used in the not-loaned-to-zircon sense.
    ZX_ASSERT(lower_used_bitmap_[i]);
  }
  ZX_ASSERT(!range.empty());
  ZX_ASSERT(lower_ranges_.find(range) == lower_ranges_.end());
}

void ProtectedRangesTest::CheckRangeDel(const TestRange& range) {
  // Still needs to be used in not-loaned-to-zircon sense at time of deletion.
  for (uint64_t i = range.begin(); i != range.end(); ++i) {
    ZX_ASSERT(lower_used_bitmap_[i]);
  }
  if (lower_ranges_.find(range) == lower_ranges_.end()) {
    DLOG("range - begin(): %" PRIu64 " end(): %" PRIu64, range.begin(), range.end());
    for (auto lower_range : lower_ranges_) {
      DLOG("lower_range - begin(): %" PRIu64 " end(): %" PRIu64, lower_range.begin(),
           lower_range.end());
    }
    DLOG("range is missing?");
    protected_ranges_->DebugDumpBacktrace();
  }
  ZX_ASSERT(lower_ranges_.find(range) != lower_ranges_.end());
  bool found_any_needed_zeroing = false;
  for (uint64_t i = range.begin(); i != range.end(); ++i) {
    ZX_ASSERT(lower_refcounts_[i] >= 1);
    if (lower_refcounts_[i] == 1) {
      found_any_needed_zeroing = true;
    }
  }
  // Deletion of a lower range must not overlap any current upper ranges unless the lower range is
  // completely covered by other lower ranges.  We've already upper-deleted any range that led to
  // the current DelProtectedRange() or ModProtectedRange().
  //
  // In the case of ModProtectedRange(), the entire "before" lower range is permitted to experience
  // disruption of any ongoing DMA, iff any portion of the range being shortened is not covered by
  // some other range or covered by the overlap between the old_range and new_range of the
  // ModProtectedRange().
  if (found_any_needed_zeroing) {
    for (uint64_t i = range.begin(); i != range.end(); ++i) {
      // If this fires, it means an upper range is having its ongoing DMA disrupted (virtually,
      // during this test run).
      ZX_ASSERT(!upper_bitmap_[i]);
    }

    for (uint64_t i = range.begin(); i != range.end(); ++i) {
      // FW only supports zeroing any part of the range when no part of the range is overlapping
      // with any other range.
      ZX_ASSERT(lower_refcounts_[i] == 1);
    }
  }
}

void ProtectedRangesTest::CheckRangeMod(const TestRange& old_range, const TestRange& new_range) {
  ZX_ASSERT(TestRangesOverlap(old_range, new_range));
  ZX_ASSERT((old_range.begin() == new_range.begin()) || (old_range.end() == new_range.end()));

  if (new_range.length() < old_range.length()) {
    TestRange removing = TestRange::Empty();
    if (old_range.begin() == new_range.begin()) {
      removing = TestRange::BeginEnd(new_range.end(), old_range.end());
    } else {
      ZX_DEBUG_ASSERT(old_range.end() == new_range.end());
      removing = TestRange::BeginEnd(old_range.begin(), new_range.begin());
    }
    bool found_any_needed_zeroing = false;
    for (uint64_t i = removing.begin(); i != removing.end(); ++i) {
      ZX_ASSERT(lower_refcounts_[i] >= 1);
      if (lower_refcounts_[i] == 1) {
        found_any_needed_zeroing = true;
      }
    }
    if (found_any_needed_zeroing) {
      // Check that we never shorten a range such that zeroing is required and the range overlaps
      // another lower range, as we don't want to require FW to support that.
      for (uint64_t i = old_range.begin(); i != old_range.end(); ++i) {
        ZX_ASSERT(lower_refcounts_[i] == 1);
      }
    }
  }

  // The rest of the checking of CheckRangeMod() is handled by CheckRangeAdd() and CheckRangeDel(),
  // since the test implementation uses those to back ModProtectionRange() (in a way that doesn't
  // penalize the code under test for using an extra range).
}

void ProtectedRangesTest::FlushIncrementalOptimization() {
  CheckInterOpInvariants();
  while (!protected_ranges_->StepTowardOptimalRanges()) {
    CheckInterOpInvariants();
  }
  CheckInterOpInvariants();
}

bool ProtectedRangesTest::TestRangesOverlap(const TestRange& a, const TestRange& b) {
  if (a.end() <= b.begin()) {
    return false;
  }
  if (b.end() <= a.begin()) {
    return false;
  }
  return true;
}

// This is "mini" stress in the sense that we don't run it for a huge amount of time in CQ, and in
// the sense that it's a unit test, not hooked to the rest of sysmem, aml-securemem, TEE, BL32, HW.
//
// However, given the single-threaded nature of sysmem, this unit test should do a good job finding
// any cases that we're handling completely wrong.  This test is not intended to require big updates
// if we change which ranges we choose to fix up first for optimization reasons, so this test does
// not check if the intended optimizations are doing what's expected, only that incremental
// optimization does complete without endlessly requesting more calls, and that invariants stay
// true for every step.  In other words, this test is checking for a functionally correct
// implementation, but not necessarily an optimizing implementation.  We can use other less-random
// unit tests to cover the specific optimizations we want to validate one by one.
TEST_F(ProtectedRangesTest, MiniStress) {
  // Setup() called CheckInvariants().

  DLOG("seed: %" PRIx64, seed_);

  constexpr uint64_t kIterations = 100000;
  constexpr uint64_t kPickOpEnd = 100;
  std::uniform_int_distribution<uint32_t> distribution(0, kPickOpEnd);
  for (uint64_t iteration_ordinal = 0; iteration_ordinal < kIterations; ++iteration_ordinal) {
    if (iteration_ordinal % 1000 == 0) {
      DLOG("iteration_ordinal: %" PRIu64, iteration_ordinal);
    }
    uint32_t pick_op = distribution(prng_) % kPickOpEnd;
    switch (pick_op) {
      case 0 ... 39: {
        ZX_ASSERT(upper_used_ <= paddr_size_);
        if (upper_used_ == paddr_size_) {
          break;
        }
        AddRandomRange();
        break;
      }
      case 40 ... 79: {
        if (upper_ranges_.empty()) {
          break;
        }
        RemoveRandomRange();
        break;
      }
      case 80 ... 99: {
        // This intentionally sometimes causes StepTowardOptimalRanges() to be called extra times,
        // which is allowed.
        FlushIncrementalOptimization();
        break;
      }
      default: {
        // Impossible, assuming above case labels cover all the possible values.
        ZX_PANIC("The case label ranges don't cover all possible values?");
      }
    }
  }

  while (!upper_ranges_.empty()) {
    RemoveRandomRange();
  }

  // TearDown() will call CheckInvariants(); CheckLeaks().
}
