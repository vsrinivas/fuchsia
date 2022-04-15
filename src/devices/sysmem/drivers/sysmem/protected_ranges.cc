// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "protected_ranges.h"

#include <lib/fit/defer.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <string>

#include <fbl/algorithm.h>

#include "macros.h"
#include "src/lib/fxl/strings/string_printf.h"

#define DLOG_ENABLED 0
#define BACKTRACE_DLOG 0

#if DLOG_ENABLED
static bool dynamic_dlog_enabled = true;
#if BACKTRACE_DLOG
static constexpr uint32_t kBacktraceLines = 1024;
static std::string backtrace[kBacktraceLines];
static std::atomic<uint32_t> next_backtrace_line = 0;
#define DLOG(fmt, ...)                                         \
  do {                                                         \
    if (!dynamic_dlog_enabled) {                               \
      break;                                                   \
    }                                                          \
    uint32_t __line = next_backtrace_line++;                   \
    __line = __line % kBacktraceLines;                         \
    backtrace[__line] = fxl::StringPrintf(fmt, ##__VA_ARGS__); \
  } while (0)
static void DumpBacktrace() {
  // We don't worry about dumping a few empty strings if we haven't even gotten a cycle yet; just
  // for debugging.
  LOG(INFO, "################# BACKTRACE BEGIN ###################");
  uint32_t line = next_backtrace_line % kBacktraceLines;
  for (uint32_t i = 0; i < kBacktraceLines; ++i, line = (line + 1) % kBacktraceLines) {
    LOG(INFO, "%s", backtrace[line].c_str());
  }
  LOG(INFO, "################# BACKTRACE END ###################");
}
#else
#define DLOG(fmt, ...)             \
  do {                             \
    if (!dynamic_dlog_enabled) {   \
      break;                       \
    }                              \
    LOG(INFO, fmt, ##__VA_ARGS__); \
  } while (0)
#endif
#else
#define DLOG(fmt, ...)
#endif

namespace {

using ProtectedRanges = protected_ranges::ProtectedRanges;
using Ranges = protected_ranges::ProtectedRanges::Ranges;
using Range = protected_ranges::Range;

bool IsAnyInternalOverlap(const Ranges& ranges) {
  uint64_t last_end = 0;
  for (auto& range : ranges) {
    if (last_end > range.begin()) {
      return true;
    }
    last_end = range.end();
  }
  return false;
}

// Is a covered by b?
bool IsCoveredBy(const Range& a, const Range& b) {
  return b.begin() <= a.begin() && b.end() >= a.end();
}

// Return a - b, in the CSG sense of no negative ranges in the result.  The result.first is what's
// left over on the left, and result.second is what's left over on the right.  Either or both can
// be empty() depending on which parts of a are covered by b.
std::pair<Range, Range> SubtractRanges(const Range& a, const Range& b) {
  // Caller must ensure this.
  ZX_DEBUG_ASSERT(Range::IsOverlap(a, b));
  Range leftover_left = Range::BeginLength(a.begin(), 0);
  Range leftover_right = Range::BeginLength(a.end(), 0);
  if (b.begin() > a.begin()) {
    leftover_left = Range::BeginEnd(a.begin(), b.begin());
  }
  if (b.end() < a.end()) {
    leftover_right = Range::BeginEnd(b.end(), a.end());
  }
  return std::make_pair(std::move(leftover_left), std::move(leftover_right));
}

const Range* FindRangeToDelete(const Ranges& old_ranges, const Ranges& new_ranges) {
  // Try to find an old range which has no overlap with any new range.
  for (auto& old_range : old_ranges) {
    auto [look_start, look_end] =
        ProtectedRanges::IteratorsCoveringPotentialOverlapsOfRangeWithRanges(old_range, new_ranges);
    bool found_overlap = false;
    for (auto look = look_start; look != look_end; ++look) {
      if (Range::IsOverlap(old_range, *look)) {
        found_overlap = true;
        break;
      }
    }
    if (!found_overlap) {
      return &old_range;
    }
  }
  return nullptr;
}

const Range* FindRangeToShorten(const Ranges& old_ranges, const Ranges& new_ranges,
                                Range* shorter_range) {
  // Try to find an old range that has a portion at the beginning or a portion at the end which is
  // not overlapping any new range.  Which old range and which end of that range to pick doesn't
  // matter because the caller will process all shorten ops quickly.  If an old range can be
  // shortened at both the beginning and end, we indicate these ops separately so that
  // DoOpShortenRange() only has to deal with shortening at one end or the other, not both.  We
  // could be a little more efficient by returning a list of shorten ops to do (collected in one
  // pass), but leaving the function signature this way makes the calling code more consistent
  // across the different ops (some of which get spread out in time instead).
  for (auto& old_range : old_ranges) {
    std::optional<uint64_t> min_overlapping_range_begin;
    std::optional<uint64_t> max_overlapping_range_end;
    auto [look_start, look_end] =
        ProtectedRanges::IteratorsCoveringPotentialOverlapsOfRangeWithRanges(old_range, new_ranges);
    for (auto look = look_start; look != look_end; ++look) {
      if (!Range::IsOverlap(old_range, *look)) {
        continue;
      }
      const Range& new_range = *look;
      if (!min_overlapping_range_begin || new_range.begin() < *min_overlapping_range_begin) {
        min_overlapping_range_begin = {new_range.begin()};
      }
      if (!max_overlapping_range_end || new_range.end() > *max_overlapping_range_end) {
        max_overlapping_range_end = {new_range.end()};
      }
    }
    ZX_DEBUG_ASSERT(!!min_overlapping_range_begin == !!max_overlapping_range_end);
    if (min_overlapping_range_begin && *min_overlapping_range_begin > old_range.begin()) {
      *shorter_range = Range::BeginEnd(*min_overlapping_range_begin, old_range.end());
      return &old_range;
    }
    if (max_overlapping_range_end && *max_overlapping_range_end < old_range.end()) {
      *shorter_range = Range::BeginEnd(old_range.begin(), *max_overlapping_range_end);
      return &old_range;
    }
  }
  return nullptr;
}

const Range* FindBestSplit(const Ranges& old_ranges, const Ranges& new_ranges,
                           Range* new_gap_to_stop_using) {
  // Find the largest gap in new_ranges that's completely covered by an interior portion (not
  // touching either extreme) of a range in old_ranges.
  std::optional<Range> best_gap;
  const Range* best_gap_old_range = nullptr;
  for (auto& old_range : old_ranges) {
    auto [look_start, look_end] =
        ProtectedRanges::IteratorsCoveringPotentialOverlapsOfRangeWithRanges(old_range, new_ranges);
    const Range old_interior = Range::BeginEnd(old_range.begin() + 1, old_range.end() - 1);
    std::optional<uint64_t> prev_end;
    for (auto look = look_start; look != look_end; prev_end = {look->end()}, ++look) {
      if (!prev_end) {
        continue;
      }
      Range new_gap = Range::BeginEnd(*prev_end, look->begin());
      if (best_gap && new_gap.length() <= best_gap->length()) {
        continue;
      }
      if (IsCoveredBy(new_gap, old_interior)) {
        ZX_DEBUG_ASSERT(!best_gap || new_gap.length() > best_gap->length());
        best_gap.emplace(std::move(new_gap));
        best_gap_old_range = &old_range;
      }
    }
  }
  ZX_DEBUG_ASSERT(!!best_gap == !!best_gap_old_range);
  if (!best_gap) {
    return nullptr;
  }
  *new_gap_to_stop_using = std::move(*best_gap);
  return best_gap_old_range;
}

const Range* FindBestMerge(const Ranges& old_ranges, const Ranges& new_ranges,
                           const Range** second_range_to_merge) {
  // Find the smallest gap in old_ranges that's covered by new_ranges (which since new_ranges is
  // coalesced, will be a single range of new_ranges doing the covering).
  std::optional<uint64_t> best_gap_size;
  const Range* best_gap_left_range = nullptr;
  const Range* best_gap_right_range = nullptr;

  const Range* prev_range = nullptr;
  for (auto& old_range : old_ranges) {
    auto set_prev_range = fit::defer([&prev_range, &old_range] { prev_range = &old_range; });
    if (!prev_range) {
      continue;
    }
    const Range old_gap = Range::BeginEnd(prev_range->end(), old_range.begin());
    if (best_gap_size && old_gap.length() >= *best_gap_size) {
      continue;
    }
    auto [look_start, look_end] =
        ProtectedRanges::IteratorsCoveringPotentialOverlapsOfRangeWithRanges(old_gap, new_ranges);
    for (auto look = look_start; look != look_end; ++look) {
      if (IsCoveredBy(old_gap, *look)) {
        ZX_DEBUG_ASSERT(!best_gap_size || old_gap.length() < *best_gap_size);
        best_gap_size = {old_gap.length()};
        best_gap_left_range = prev_range;
        best_gap_right_range = &old_range;
      }
    }
  }
  ZX_DEBUG_ASSERT(!!best_gap_size == !!best_gap_left_range);
  ZX_DEBUG_ASSERT(!!best_gap_size == !!best_gap_right_range);
  if (!best_gap_size) {
    return nullptr;
  }
  *second_range_to_merge = best_gap_right_range;
  return best_gap_left_range;
}

Range AlignRequestedRange(const Range& range, uint64_t alignment) {
  uint64_t aligned_start = fbl::round_down(range.begin(), alignment);
  uint64_t aligned_end = fbl::round_up(range.end(), alignment);
  return Range::BeginEnd(aligned_start, aligned_end);
}

}  // namespace

namespace protected_ranges {

ProtectedRanges::ProtectedRanges(ProtectedRangesControl* ranges_control)
    : ranges_control_(ranges_control) {
  ZX_DEBUG_ASSERT(ranges_control_);
  is_dynamic_ = ranges_control_->IsDynamic();
  max_range_count_ = ranges_control_->MaxRangeCount();
  ZX_DEBUG_ASSERT(max_range_count_ >= 1);
  is_mod_available_ = ranges_control_->HasModProtectedRange();
  if (is_dynamic_) {
    max_logical_range_count_ = is_mod_available_ ? max_range_count_ - 1 : max_range_count_ - 2;
  } else {
    max_logical_range_count_ = 1;
  }
  range_granularity_ = ranges_control_->GetRangeGranularity();
  // We allow 1 byte granularity here for testing purposes mainly.  Actual granularity is extremely
  // likely to be at least 4KiB.
  ZX_DEBUG_ASSERT(range_granularity_ >= 1);
}

ProtectedRanges::~ProtectedRanges() { ZX_ASSERT(ranges_.empty()); }

uint64_t ProtectedRanges::max_logical_ranges() { return max_logical_range_count_; }

bool ProtectedRanges::AddRange(const Range& range_param) {
  DLOG("AddRange() begin:");
  DebugDumpRangesForUnitTest(requested_ranges_, "requested_ranges_");
  DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
  DebugDumpRangesForUnitTest(ranges_, "ranges_");
  DebugDumpRangeForUnitTest(range_param, "range");

  if (!is_dynamic_ && !requested_ranges_.empty()) {
    // The AddRange() is logically successful, but no need to track anything
    // per-range after the first range, whose required_range is the entire
    // extent of the space being managed.
    return true;
  }

  const Range& range = *requested_ranges_.emplace(range_param.Clone());
  requested_bytes_ += range.length();

  // We know that requested_ranges_ has no internal overlap.
  ZX_DEBUG_ASSERT(!IsAnyInternalOverlap(requested_ranges_));
  Range new_required_range = AlignRequestedRange(range, range_granularity_);
  if (!is_dynamic_) {
    // If !is_dynamic_, we only get to add one range, which is the entire extent of the space being
    // managed.
    new_required_range = Range::BeginLength(ranges_control_->GetBase(), ranges_control_->GetSize());
  }
  const auto required_range_iter = required_ranges_.emplace(std::move(new_required_range));
  const Range& required_range = *required_range_iter;
  PropagateRequiredRangesToGoalRanges(required_range);
  if (!FixupRangesDuringAdd(required_range)) {
    // When !is_dynamic_, we won't reach here because pages were never loaned back to Zircon, so
    // the commit during fixup will work.
    ZX_DEBUG_ASSERT(is_dynamic_);
    DLOG("!FixupRangesDuringAdd()");

    requested_bytes_ -= range.length();
    requested_ranges_.erase(range);

    // Only erase one if there are multiple that match.
    auto stash_required_range = required_range.Clone();
    required_ranges_.erase(required_ranges_.find(required_range));
    PropagateRequiredRangesToGoalRanges(stash_required_range);

    DebugDumpRangesForUnitTest(requested_ranges_, "requested_ranges_");
    DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
    DebugDumpRangesForUnitTest(ranges_, "ranges_");

    return false;
  }

  // Zero the newly requested range using the TEE.  This way any protected mode devices will see
  // the new buffer as filled with zeroes, instead of whatever REE-written zeroes might end up
  // looking like post-scramble.  In testing situations we pretend as if this is allowed at
  // arbitrary granularity, but in actual use (so far) this will assert that range is aligned at
  // page boundaries (partly because that's the smallest zeroing granularity that the TEE allows, by
  // design).
  if (is_dynamic_) {
    ranges_control_->ZeroProtectedSubRange(true, range);
  }

  DLOG("AddRange() end (success):");
  DebugDumpRangesForUnitTest(requested_ranges_, "requested_ranges_");
  DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
  DebugDumpRangesForUnitTest(ranges_, "ranges_");

  return true;
}

void ProtectedRanges::DeleteRange(const Range& range) {
  DLOG("DeleteRange() begin");
  DebugDumpRangesForUnitTest(requested_ranges_, "requested_ranges_");
  DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
  DebugDumpRangesForUnitTest(ranges_, "ranges_");

  if (!is_dynamic_) {
    // At this layer, delete isn't possible if !is_dynamic_.
    return;
  }

  ZX_DEBUG_ASSERT(requested_ranges_.find(range) != requested_ranges_.end());

  requested_ranges_.erase(range);
  requested_bytes_ -= range.length();

  Range required_range = AlignRequestedRange(range, range_granularity_);
  // Only erase one if there are multiple that match.
  required_ranges_.erase(required_ranges_.find(required_range));
  PropagateRequiredRangesToGoalRanges(required_range);
  FixupRangesDuringDelete(required_range);

  DLOG("DeleteRange() end");
  DebugDumpRangesForUnitTest(requested_ranges_, "requested_ranges_");
  DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
  DebugDumpRangesForUnitTest(ranges_, "ranges_");
}

bool ProtectedRanges::FixupRangesDuringAdd(const Range& new_required_range) {
  DLOG("FixupRangesDuringAdd() begin");
#if DLOG_ENABLED
  auto at_end = fit::defer([] { DLOG("FixupRangesDuringAdd() end"); });
#endif

  // The goal of this method is to fix up ranges_ just enough to put new_required_range under a
  // range in ranges_ (and in HW), so we can complete the AddRange().  In practice, we'll only see
  // allocations at or very near the bottom of protected_memory_size or adjacent or nearly adjacent
  // to the end of an existing range (the "near" and "nearly" is because of alignment requirements
  // and plumbing those to RegionAllocator, which isn't done as of this comment, but is TODO).
  // Other placements are covered by tests in terms of logical correctness, but those other
  // placements are not relevant to efficiency in practice.
  //
  // During range deletion, we immediately opportunistically delete / shorten if we can, so this
  // fixup during add will never see a case where the new_required_range is initially touching more
  // than 1 range in ranges_ at the start and 1 range in ranges_ at the end.
  //
  // Before the end of any op, we oportunistically coalesce ranges transiently in ranges_ if we can,
  // so this fixup during add can assume that ranges_ has no ranges that are touching each other.
  // In other words, a range of adjacent blocks that are protected are protected by a single range
  // in ranges_, not by multiple ranges in ranges_.
  //
  // Steps:
  //
  // If ranges_ is completely empty, we can just add new_required_range to ranges_.
  //
  // Check if new_required_range is already fully covered by ranges_.  If so, we don't need to do
  // anything more.
  //
  // If any portion of new_required_range is not covered by ranges_, try to find the up-to-one range
  // whose end we can increase to cover the not-presently-covered portion of new_required_range.
  // Before returning we'll try to coalesce in case we can reduce the number of ranges in ranges_
  // without needing to call UseRange() (handled by DoOpExtendRangeEnd()).
  //
  // If there's no range in ranges_ whose end we can increase to cover new_required_range, then
  // if ranges_ is under max_logical_range_count_, add a new range to ranges_ just for
  // new_required_range (avoiding creating any overlap) and try to coalesce before returning.
  //
  // If ranges_.size() == max_logical_range_count_, provisionally add a new range just for
  // new_required_range (1 over max_logical_range_count_), and then merge the two adjacent blocks
  // with the smallest gap between them to get back down to max_logical_range_count_ (1 or 2 over
  // max_logical_range_count_ depending on is_mod_available).  If any UseRange() fails, roll state
  // back without any more calls to UseRange(), and return false.

  if (ranges_.empty()) {
    DLOG("ranges_.empty()");
    if (!DoOpAddRange(new_required_range)) {
      return false;
    }
    return true;
  }

  const auto range_starting_after_iter = ranges_.upper_bound(
      Range::BeginLength(new_required_range.begin(), std::numeric_limits<uint64_t>::max()));
  if (range_starting_after_iter != ranges_.begin()) {
    DLOG("append to previous range");
    // There is range starting at or before new_required_range.
    auto range_starting_at_or_before_iter = range_starting_after_iter;
    --range_starting_at_or_before_iter;
    // Check if covered already.
    if (range_starting_at_or_before_iter->begin() <= new_required_range.begin() &&
        range_starting_at_or_before_iter->end() >= new_required_range.end()) {
      DLOG("but already covered");
      // already covered; no modifications needed
      return true;
    }
    // The range_starting_at_or_before_iter can be extended to cover new_required_range.  We need
    // to avoid creating overlap, so we clamp using range_starting_after_iter->begin().  We know if
    // range_starting_after_iter->begin() is before new_required_range.end(), then the former will
    // cover the rest of the latter.
    uint64_t new_begin = range_starting_at_or_before_iter->begin();
    uint64_t new_end = new_required_range.end();
    if (range_starting_after_iter != ranges_.end()) {
      new_end = std::min(new_end, range_starting_after_iter->begin());
    }
    // Since RegionAllocator will place new ranges as close as possible to a previous allocated
    // region, the strategy of just extending the previous block is more efficient in practice than
    // it would be if ranges were added completely randomly.  In tests we cover adding random ranges
    // and we expect it to work, but efficiency of adding a range with a big gap after the previous
    // range isn't a concern since that doesn't really happen outside of tests (at least for now).
    if (!DoOpExtendRangeEnd(*range_starting_at_or_before_iter,
                            Range::BeginEnd(new_begin, new_end))) {
      DLOG("!DoOpExtendRangeEnd()");
      return false;
    }
    // We coalesce so that we can assert that ranges_ is coalesced between ops.
    TryCoalesceAdjacentRangesAt(&ranges_, new_end);

    DebugDumpRangesForUnitTest(ranges_, "ranges_");

    return true;
  }

  // We can add a range, but we need to avoid creating any overlap.  It's possible the range after
  // the new range is starting at the last block of the range being added, so in that case we add
  // a range that's one block shorter than new_required_range to avoid overlap, and the coalesce
  // within DoOpAddRange() takes care of coalescing away the non-overlapping barely-touching
  // boundary.
  uint64_t new_end = std::min(new_required_range.end(), range_starting_after_iter->begin());
  auto adjusted_range_to_add = Range::BeginEnd(new_required_range.begin(), new_end);

  // If the new_end is the same as range_starting_after_iter->begin(), that means we'll get to
  // coalesce which means we don't have to worry about potentially needing to merge some other
  // pair of adjacent (but not touching) ranges, and we don't have to worry about needing to
  // UseRange() (which can fail) for some other gap for a merge in addition to the UseRange() for
  // the DoOpAddRange().  If not the same, then we know we can roll back the AddRange by just
  // removing adjusted_range_to_add since it won't have been coalesced.  And of course we don't have
  // any coalescing happening at the beginning of the range we're adding here because there was no
  // prior range (which would have been found above and extended to cover instead of ending up
  // here).

  if (!DoOpAddRange(adjusted_range_to_add)) {
    return false;
  }

  if (ranges_.size() <= max_logical_range_count_) {
    // We covered the range and we're still under max_logical_range_count_, so we're done.
    return true;
  }
  ZX_DEBUG_ASSERT(ranges_.size() == max_logical_range_count_ + 1);

  // Now we merge a couple other ranges that are relatively close together to get back down to
  // max_logical_range_count_.  If UseRange() fails we know we can just remove adjusted_range_to_add
  // since we know we didn't coalesce above.
  ZX_DEBUG_ASSERT(ranges_.find(adjusted_range_to_add) != ranges_.end());

  const Range* merge_right_range;
  auto merge_left_range = FindBestMerge(ranges_, goal_ranges_, &merge_right_range);
  ZX_DEBUG_ASSERT(merge_left_range);
  if (!DoOpMergeRanges(*merge_left_range, *merge_right_range)) {
    // Roll back the DoOpAddRange() above.  This also does the UnUseRange().
    DoOpDelRange(adjusted_range_to_add);
    return false;
  }
  ZX_DEBUG_ASSERT(ranges_.size() == max_logical_range_count_);

  return true;
}

void ProtectedRanges::FixupRangesDuringDelete(const Range& old_required_range) {
  // Since we can only be using max_logical_range_count_ ranges by the time the delete is done,
  // there are plenty of situations where we can't UnUseRange() on any part of old_required_range
  // despite old_required_range itself no longer being required.
  //
  // Instead of trying to do fixup that's particularly specific to delete, we can run as much
  // genereral incremental optimization as we can without needing to call UseRange().
  while (!StepTowardOptimalRangesInternal(false))
    ;
}

void ProtectedRanges::DebugDumpRangeForUnitTest(const Range& range, const char* info) {
  if constexpr (!DLOG_ENABLED) {
    return;
  }
#if DLOG_ENABLED
  if (!dynamic_dlog_enabled) {
    return;
  }
#endif
  uint64_t base = ranges_control_->GetBase();
  uint64_t size = ranges_control_->GetSize();
  uint64_t granularity = ranges_control_->GetRangeGranularity();
  ZX_ASSERT(base % granularity == 0);
  ZX_ASSERT(size % granularity == 0);
  std::string line;
  for (uint64_t i = base; i < range.begin(); ++i) {
    line += '.';
  }
  for (uint64_t i = range.begin(); i < range.end(); ++i) {
    line += 'R';
  }
  for (uint64_t i = range.end(); i < base + size; ++i) {
    line += '.';
  }
  DLOG("%s - %s", line.c_str(), info);
}

void ProtectedRanges::DebugDumpRangesForUnitTest(const Ranges& ranges, const char* info) {
  if constexpr (!DLOG_ENABLED) {
    return;
  }
#if DLOG_ENABLED
  if (!dynamic_dlog_enabled) {
    return;
  }
#endif
  uint32_t range_ordinal = 0;
  uint64_t base = ranges_control_->GetBase();
  uint64_t size = ranges_control_->GetSize();
  uint64_t granularity = ranges_control_->GetRangeGranularity();
  ZX_ASSERT(base % granularity == 0);
  ZX_ASSERT(size % granularity == 0);
  uint64_t prev_end = base;
  // We're not that concerned about efficiency for this method since we never call it outside
  // debugging and unit tests.
  std::string line;
  for (auto& iter : ranges) {
    ZX_ASSERT(!iter.empty());
    if (iter.begin() < prev_end) {
      DLOG("%s", line.c_str());
      line = "";
      prev_end = 0;
    }
    for (uint64_t j = prev_end; j < iter.begin(); ++j) {
      line += '_';
    }
    for (uint64_t j = iter.begin(); j != iter.end(); ++j) {
      line += '0' + range_ordinal % 10;
    }
    prev_end = iter.end();
    ++range_ordinal;
  }
  if (size > prev_end - base) {
    for (uint64_t j = prev_end; j != base + size; ++j) {
      line += '_';
    }
  }
  ZX_ASSERT(!line.empty());
  DLOG("%s - %s", line.c_str(), info);
}

void ProtectedRanges::DebugDumpOffset(uint64_t offset, const char* info) {
  if constexpr (!DLOG_ENABLED) {
    return;
  }
#if DLOG_ENABLED
  if (!dynamic_dlog_enabled) {
    return;
  }
#endif
  uint64_t base = ranges_control_->GetBase();
  uint64_t size = ranges_control_->GetSize();
  uint64_t granularity = ranges_control_->GetRangeGranularity();
  ZX_ASSERT(base % granularity == 0);
  ZX_ASSERT(size % granularity == 0);
  std::string line;
  for (uint64_t i = base; i < offset; ++i) {
    line += '.';
  }
  line += '^';
  for (uint64_t i = offset + 1; i < base + size; ++i) {
    line += '.';
  }
  DLOG("%s - %s", line.c_str(), info);
}

void ProtectedRanges::DebugDumpBacktrace() {
#if DLOG_ENABLED && BACKTRACE_DLOG
  DumpBacktrace();
#endif
}

void ProtectedRanges::DynamicSetDlogEnabled(bool enabled) {
#if DLOG_ENABLED && BACKTRACE_DLOG
  dynamic_dlog_enabled = enabled;
#endif
}

bool ProtectedRanges::StepTowardOptimalRanges() {
  // Here we know that ranges_ won't have any internal overlap.  At other places within ops in
  // StepTowardOptimalRangesInternal(), this won't be true.
  bool result = StepTowardOptimalRangesInternal(true);
  return result;
}

bool ProtectedRanges::StepTowardOptimalRangesInternal(bool allow_use_range) {
  DLOG("StepTowardOptimalRangesInternal(%u) begin", allow_use_range);
  DebugDumpRangesForUnitTest(requested_ranges_, "requested_ranges_");
  DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
  DebugDumpRangesForUnitTest(ranges_, "ranges_");

#if DLOG_ENABLED
  auto log_when_done = fit::defer([this, allow_use_range] {
    DLOG("StepTowardOptimalRangesInternal(%u) end", allow_use_range);
    DebugDumpRangesForUnitTest(requested_ranges_, "requested_ranges_");
    DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
    DebugDumpRangesForUnitTest(ranges_, "ranges_");
  });
#endif

  // It's fine that this way of checking will result in one extra call after the previous call made
  // the last needed change.
  if (ranges_ == goal_ranges_) {
    DLOG("ranges_ == goal_ranges_");
    return true;
  }

  // We must find at least one thing we can do to get ranges_ closer to goal_ranges_ before
  // returning.
  //
  // Both ranges_ and goal_ranges_ are maintained as fully coalesced, so we need to ensure that
  // ranges_ is always coalesced before returning.
  //
  // Priority:
  //  * clean delete
  //  * clean shorten
  //  * if ranges_.size() < max_logical_range_count_
  //    * split (pick max size gap) or add(ever needed?)
  //  * else
  //    * merge (pick min size gap) then split (pick max size gap)

  auto range_to_delete = FindRangeToDelete(ranges_, goal_ranges_);
  if (range_to_delete) {
    DebugDumpRangeForUnitTest(*range_to_delete, "range_to_delete");

    DoOpDelRange(*range_to_delete);
    return false;
  }

  Range shorter_range;
  auto range_to_shorten = FindRangeToShorten(ranges_, goal_ranges_, &shorter_range);
  if (range_to_shorten) {
    DebugDumpRangeForUnitTest(*range_to_shorten, "range_to_shorten");
    DebugDumpRangeForUnitTest(shorter_range, "shorter_range");

    DoOpShortenRange(*range_to_shorten, shorter_range);
    return false;
  }

  ZX_DEBUG_ASSERT(ranges_.size() <= max_logical_range_count_);

  // We know that once we're done with splits, we're also done with merges.  Intuitively this can be
  // understood by considering that if we didn't need to do a merge to achieve a split elsewhere, we
  // wouldn't have the merge as a goal in the first place.  The count of implied splits is always >=
  // the count of implied merges.
  Range new_gap_to_stop_using;
  auto range_to_split = FindBestSplit(ranges_, goal_ranges_, &new_gap_to_stop_using);
  if (range_to_split) {
    DebugDumpRangeForUnitTest(*range_to_split, "range_to_split");
    DebugDumpRangeForUnitTest(new_gap_to_stop_using, "new_gap_to_stop_using");

    // Merge first if we need to reclaim a range overall, since merge can fail so it's easier to
    // unwind if merge doesn't work.  When merge does work (now or a little while later), we'll
    // quickly also split (which won't fail) which will allow other pages to be loaned to Zircon.
    //
    // The sequence of merges and splits to get ranges_ the rest of the way to goal_ranges_ is the
    // reason we spread out calls to StepTowardOptimalRanges() using a timer, to give Zircon a
    // chance to start using the newly-loaned pages before we reclaim yet other pages for another
    // merge.
    if (ranges_.size() == max_logical_range_count_) {
      if (!allow_use_range) {
        DLOG("ranges_.size() == max_logical_range_count_ && !allow_use_range");
        // We can't do any more without calling UseRange(), so we shouldn't do any more immediately
        // (and quickly) during DeleteRange().
        return true;
      }
      const Range* second_range_to_merge;
      auto first_range_to_merge = FindBestMerge(ranges_, goal_ranges_, &second_range_to_merge);
      if (first_range_to_merge) {
        DebugDumpRangeForUnitTest(*first_range_to_merge, "first_range_to_merge");
        DebugDumpRangeForUnitTest(*second_range_to_merge, "second_range_to_merge");

        if (!DoOpMergeRanges(*first_range_to_merge, *second_range_to_merge)) {
          // We want to get called again to try this merge again fairly soon.  UseRange() failed,
          // but it should succeed in a little while.  The sooner we can succeed the UseRange(),
          // the sooner we can do a split to loan some other pages to Zircon, and the sooner we can
          // reach goal_ranges_, which overall will be loaning as many pages to Zircon as possible.
          // But unfortunately it's quite possible to get stuck with too few pages available to play
          // "slider puzzle" to get closer to goal_ranges_.
          return false;
        }
        // Now that any needed merge is done we can do a split, but the split we do won't
        // necessarily be the same split we found before, because the merge may have merged the
        // range we found before with another range, so we need to find the/a split again just in
        // case it has changed due to the merge.
        //
        // If there was a split to do before, and all we've done since then is merge, we do know
        // that there will still be at least that gap to stop using now, so at least one split to do
        // now.
        range_to_split = FindBestSplit(ranges_, goal_ranges_, &new_gap_to_stop_using);
      }
    }
    ZX_ASSERT(range_to_split);
    DoOpSplitRange(*range_to_split, new_gap_to_stop_using);
    return false;
  }

  ZX_PANIC("Failed to match any update cases; no progress made.\n");
  // We won't reach here, but since we haven't reached goal_ranges_, logically we'd return false if
  // we reached here, despite having made no progress.
  return false;
}

std::pair<Ranges::iterator, Ranges::iterator>
ProtectedRanges::IteratorsCoveringPotentialOverlapsOfRangeWithRanges(const Range& range,
                                                                     const Ranges& ranges) {
  // For an r in ranges to intersect with the first byte of range, the r must begin <=
  // range.begin().  We need to find the first such range which also has r.end() > range.begin(),
  // if any.
  //
  // All prior ranges will not overlap because either ranges is not allowed to have overlapping
  // ranges, or in the case of required_ranges_, there can be overlap/duplicates, but the
  // overlap/duplication is highly restricted.  The restrictions imply that any two ranges a and b
  // in required_ranges_ will satisfy (a.begin() <= b.begin()) == (a.end() <= b.end()).  This
  // restriction is asserted in protected_ranges_test.cc.
  //
  // First we get the first r in ranges with r.begin() >= range.begin().
  auto first_ge_begin = ranges.lower_bound(Range::BeginLength(range.begin(), 0));
  auto look_begin = first_ge_begin;
  // This loop isn't really adding any time complexity overall since the caller will be iterating
  // over all these ranges anyway.
  while (look_begin != ranges.begin() &&
         (look_begin == ranges.end() || look_begin->end() > range.begin())) {
    --look_begin;
  }
  // Bump look_begin forward again if it turns out that the current look_begin is entirely before
  // range, so the caller doesn't really need to look at the current look_begin.
  if (look_begin != ranges.end() && look_begin->end() <= range.begin()) {
    ++look_begin;
  }

  // For an r in ranges to intersect with the last block of range, the r must have r.begin() <
  // range.end() (in other words r.begin() <= range.end() - 1).  No r in ranges with r.begin() >=
  // range.end() can be overlapping range.  Since ranges is ordered, we can stop looking once we've
  // looked at the last r in ranges with r.begin() < range.end(), just prior to the first r in
  // ranges with r.begin() >= range.end().
  auto first_begin_ge_range_end = ranges.lower_bound(Range::BeginLength(range.end(), 0));
  auto look_end = first_begin_ge_range_end;

  return std::make_pair(look_begin, look_end);
}

void ProtectedRanges::PropagateRequiredRangesToGoalRanges(const Range& diff_range) {
  DLOG("PropagateRequiredRangesToGoalRanges() begin");
  DebugDumpRangeForUnitTest(diff_range, "diff_range");

  UpdateCoalescedRequiredRanges(diff_range);

  DebugDumpRangesForUnitTest(coalesced_required_ranges_, "coalesced_required_ranges_");

  UpdateInteriorUnusedRanges(diff_range);

  DebugDumpRangesForUnitTest(interior_unused_ranges_, "interior_unused_ranges_");
  ZX_DEBUG_ASSERT(coalesced_required_ranges_.empty() ||
                  interior_unused_ranges_.size() + 1 == coalesced_required_ranges_.size());

  BuildLargestInteriorUnusedRanges();

  DebugDumpRangesForUnitTest(largest_interior_unused_ranges_, "largest_interior_unused_ranges_");
  ZX_DEBUG_ASSERT(largest_interior_unused_ranges_.size() ==
                  std::min(max_logical_range_count_ - 1, interior_unused_ranges_.size()));

  BuildGoalRanges();

  DebugDumpRangesForUnitTest(goal_ranges_, "goal_ranges_");
  ZX_DEBUG_ASSERT(largest_interior_unused_ranges_.empty() ||
                  goal_ranges_.size() == largest_interior_unused_ranges_.size() + 1);
  ZX_DEBUG_ASSERT(goal_ranges_.size() <= max_logical_range_count_);

  DLOG("PropagateRequiredRangesToGoalRanges() end");
}

void ProtectedRanges::UpdateCoalescedRequiredRanges(const Range& diff_range) {
  ZX_DEBUG_ASSERT(!diff_range.empty());

  // Properties of required_ranges_ that help with this step:
  //   * ordered by (begin(), length()) (lexicographically)
  //   * only up to one range can be covering both of two adjacent blocks
  // Summary of steps:
  //   * subtract diff_range from coalesced_required_ranges_, in the CSG sense (no negative ranges
  //     in the result)
  //   * find look_start, look_end portion of required_ranges_ that is guaranteed to be all the
  //     ranges in required_ranges_ that overlap any part of diff_range; this relies on look_start
  //     being the longest of the ranges that have the same begin() as look_start (or it would also
  //     be fine if it were before that longest range with the same begin(), but it happens to be
  //     the longest of the ranges with the same begin() which is slightly nicer)
  //   * establish an empty "in_progress" range positioned at the start of diff_range with zero
  //     length
  //   * iterate [look_start, look_end), at each in_progress begin value finding the max end
  //     value (end clamped by diff_range.end()), and adding/appending that max-len range to
  //     in_progress, flushing any non-empty "in_progress" to coalesced_required_ranges_ when any
  //     gap is detected (including a gap at start of diff_range, or at the end of diff_range)
  //   * upon reaching diff_range.end(), flush in_progress
  //   * call common code to try coalesce at diff_range.begin() and at diff_range.end() in case
  //     there's a range in coalesced_required_ranges_ that is barely touching a newly-built range
  //     within diff_range
  //   * done

  auto [subtract_begin, subtract_end] =
      IteratorsCoveringPotentialOverlapsOfRangeWithRanges(diff_range, coalesced_required_ranges_);
  Ranges::iterator next;
  for (auto iter = subtract_begin; iter != subtract_end; iter = next) {
    next = iter;
    ++next;
    auto& existing_range = *iter;
    if (!Range::IsOverlap(existing_range, diff_range)) {
      continue;
    }
    auto [leftover_left, leftover_right] = SubtractRanges(existing_range, diff_range);
    coalesced_required_ranges_.erase(existing_range);
    if (!leftover_left.empty()) {
      coalesced_required_ranges_.emplace(std::move(leftover_left));
    }
    if (!leftover_right.empty()) {
      coalesced_required_ranges_.emplace(std::move(leftover_right));
    }
  }

  auto [scan_begin, scan_end] =
      IteratorsCoveringPotentialOverlapsOfRangeWithRanges(diff_range, required_ranges_);
  Range in_progress = Range::BeginLength(diff_range.begin(), 0);
  auto flush_in_progress = [this, &in_progress, &diff_range](uint64_t gap_end) {
    ZX_DEBUG_ASSERT(gap_end > in_progress.end() || gap_end == diff_range.end());
    // flush in_progress and logically skip gap
    if (!in_progress.empty()) {
      coalesced_required_ranges_.emplace(std::move(in_progress));
    }
    in_progress = Range::BeginLength(gap_end, 0);
  };
  for (auto iter = scan_begin; iter != scan_end; ++iter) {
    if (iter->begin() > in_progress.end()) {
      flush_in_progress(iter->begin());
    }
    ZX_DEBUG_ASSERT(iter->begin() < diff_range.end());
    ZX_DEBUG_ASSERT(iter->begin() <= in_progress.end());
    if (iter->end() <= in_progress.end()) {
      continue;
    }
    ZX_DEBUG_ASSERT(iter->end() > in_progress.end());
    uint64_t new_end = std::min(diff_range.end(), iter->end());
    in_progress = Range::BeginEnd(in_progress.begin(), new_end);
  }
  flush_in_progress(diff_range.end());

  TryCoalesceAdjacentRangesAt(&coalesced_required_ranges_, diff_range.begin());
  TryCoalesceAdjacentRangesAt(&coalesced_required_ranges_, diff_range.end());
}

void ProtectedRanges::UpdateInteriorUnusedRanges(const Range& diff_range) {
  DLOG("UpdateInteriorUnusedRanges() begin");
#if DLOG_ENABLED
  auto before_return = fit::defer([] { DLOG("UpdateInteriorUnusedRanges() end"); });
#endif

  // The interior unused ranges are the gaps in coalesced_required_ranges_, where a "gap" is all the
  // contiguous non-covered pages that have a range of coalesced_required_ranges_ on either side.
  // The addresses before the first coalesced_required_ranges_ range's begin(), or after the last
  // range's end(), are not considered "gaps" in this context (are not interior unused ranges).
  if (coalesced_required_ranges_.size() < 2) {
    // To have an interior range we'd need there to be any interior.
    interior_unused_ranges_.clear();
    interior_unused_ranges_by_length_.clear();
    return;
  }

  // The diff_range is the required_range that was potentially changed in
  // coalesced_required_ranges_.  Due to overlaps in required_ranges_, not all parts of diff_range
  // necessarily changed, and there are situations where none of diff_range changed.
  //
  // Our strategy is to replace a part of interior_unused_ranges_.  We remove that part and
  // re-build that part, expanding enough to account for the interior_unused_ranges_ being the
  // invert of coalesced_required_ranges_.  Expanding also picks up on a newly-added range creating
  // a newly-interior unused range, and picks up on a newly-deleted range allowing two adjacent
  // unused ranges to be merged or causing a previously-interior unused range to become no longer
  // interior.
  //
  // To delete and re-build enough of interior_unused_ranges_, we're looking for the last range in
  // coalesced_required_ranges_ that has end() < diff_range.begin(), and we're looking for the first
  // range in coalesced_required_ranges_ that has begin() > diff_range.end().  In some call paths,
  // it's possible that the changes to diff_range interact with that much extra space on one side or
  // the other or both (we count merging or splitting two ranges in unused_interior_ranges_ as
  // "interacting" with those ranges).

  DebugDumpRangeForUnitTest(diff_range, "diff_range");
  DebugDumpRangesForUnitTest(coalesced_required_ranges_, "coalesced_required_ranges_");
  DebugDumpRangesForUnitTest(interior_unused_ranges_, "interior_unused_ranges_");

  auto [rebuild_begin, rebuild_end] =
      IteratorsCoveringPotentialOverlapsOfRangeWithRanges(diff_range, coalesced_required_ranges_);
  // expand rebuild_begin to include the range before any gap overlapping with diff_range
  if (rebuild_begin != coalesced_required_ranges_.begin() &&
      (rebuild_begin == coalesced_required_ranges_.end() ||
       rebuild_begin->end() > diff_range.begin())) {
    --rebuild_begin;
  }
  ZX_DEBUG_ASSERT(rebuild_begin == coalesced_required_ranges_.begin() ||
                  rebuild_begin->end() <= diff_range.begin());
  std::optional<uint64_t> maybe_carve_begin;
  if (rebuild_begin == coalesced_required_ranges_.begin()) {
    maybe_carve_begin = {};
    DLOG("maybe_carve_begin: {}");
  } else {
    maybe_carve_begin = {rebuild_begin->end()};
    DebugDumpOffset(*maybe_carve_begin, "maybe_carve_begin");
  }
  // expand rebuild_end; below we want to iterate up to and including the first range in
  // coalesced_required_ranges_ with begin() >= diff_range.end() (include the range after any gap
  // that overlaps with diff_range), so we need to bump rebuild_end later by one range.
  ZX_DEBUG_ASSERT(rebuild_end == coalesced_required_ranges_.end() ||
                  rebuild_end->begin() >= diff_range.end());
  std::optional<uint64_t> maybe_carve_end;
  if (rebuild_end == coalesced_required_ranges_.end()) {
    maybe_carve_end = {};
    DLOG("maybe_carve_end: {}");
  } else {
    maybe_carve_end = {rebuild_end->begin()};
    DebugDumpOffset(*maybe_carve_end, "maybe_carve_end");
  }
  if (rebuild_end != coalesced_required_ranges_.end()) {
    ++rebuild_end;
  }

  // Now we find which ranges of interior_unused_ranges_ are completely covered by
  // [rebuild.begin.end(), (rebuild_end - 1).begin())
  if (!interior_unused_ranges_.empty()) {
    uint64_t carve_begin;
    uint64_t carve_end;
    if (maybe_carve_begin) {
      carve_begin = *maybe_carve_begin;
    } else {
      carve_begin = interior_unused_ranges_.begin()->begin();
    }
    DebugDumpOffset(carve_begin, "carve_begin");
    if (maybe_carve_end) {
      carve_end = *maybe_carve_end;
    } else {
      carve_end = interior_unused_ranges_.rbegin()->end();
    }
    DebugDumpOffset(carve_end, "carve_end");
    if (carve_begin < carve_end) {
      const Range carve_range = Range::BeginEnd(carve_begin, carve_end);
      DebugDumpRangeForUnitTest(carve_range, "carve_range");
      auto [carve_begin_iter, carve_end_iter] =
          IteratorsCoveringPotentialOverlapsOfRangeWithRanges(carve_range, interior_unused_ranges_);
      Ranges::iterator next;
      for (auto iter = carve_begin_iter; iter != carve_end_iter; iter = next) {
        next = iter;
        ++next;
        DebugDumpRangeForUnitTest(*iter, "*iter");
        ZX_DEBUG_ASSERT(IsCoveredBy(*iter, carve_range));
        interior_unused_ranges_by_length_.erase(*iter);
        interior_unused_ranges_.erase(iter);
      }
    }
  }

  // Now we can find the gaps implied by [rebuild_begin, rebuild_end) and put those in
  // interior_unused_ranges_.
  std::optional<uint64_t> prev_end;
  for (auto iter = rebuild_begin; iter != rebuild_end; prev_end = {iter->end()}, ++iter) {
    if (!prev_end) {
      continue;
    }
    auto gap_range = Range::BeginEnd(*prev_end, iter->begin());
    interior_unused_ranges_.emplace(gap_range.Clone());
    interior_unused_ranges_by_length_.emplace(std::move(gap_range));
  }

  // No need to coalesce since we expanded enough to find complete ranges in interior_unused_ranges_
  // that we remove in their entirety and then we add ranges that won't be touching each other or
  // the untouched range prior or subsequent.
}

void ProtectedRanges::BuildLargestInteriorUnusedRanges() {
  // The largest_interior_unused_ranges_ are the top (up to) max_logical_ranges_count_ - 1 ranges
  // from interior_unused_ranges_by_length_, sorted by position instead of by length.
  //
  // Since largest_interior_unused_ranges_.size() is limited by max_logical_ranges_count_ - 1,
  // there's not much reason to bother doing the update incrementally.  Instead we can just toss the
  // current ranges and re-build.  If we needed to do this incrementally it'd pretty much boil down
  // to just using the last part of interior_unused_ranges_by_length_ instead of having separate
  // Ranges for this, but at least for now it's nice to have this explicitly separate for debug and
  // test.

  largest_interior_unused_ranges_.clear();
  uint64_t ranges_to_use =
      std::min(max_logical_range_count_ - 1, interior_unused_ranges_by_length_.size());
  uint64_t range_count = 0;
  for (auto iter = interior_unused_ranges_by_length_.rbegin(); range_count != ranges_to_use;
       ++range_count, ++iter) {
    largest_interior_unused_ranges_.emplace((*iter).Clone());
  }
}

void ProtectedRanges::BuildGoalRanges() {
  // goal_ranges_ are the up to max_logical_ranges_count_ - 2 gaps in
  // largest_interior_unused_ranges_ plus a range at the start and end that covers the first+ and
  // last- blocks in coalesced_required_ranges_ without covering any blocks in
  // largest_interior_unused_ranges_.

  goal_ranges_.clear();
  // Peel off the cases that are so degenerate that they would just create more conditionals (or
  // more min()/max()) to account for them further down if we didn't peel them off here.
  if (coalesced_required_ranges_.empty()) {
    return;
  } else if (coalesced_required_ranges_.size() == 1) {
    goal_ranges_.emplace((*coalesced_required_ranges_.begin()).Clone());
    return;
  }

  // At this point we know we have at least one interior unused range, but unless we have two, we
  // won't actually find any gaps.  May as well let the loop find out there are no gaps rather than
  // checking separately.
  ZX_DEBUG_ASSERT(coalesced_required_ranges_.size() >= 2);
  // If just 1 range, the loop body will run only once and never build any gap_range(s).
  ZX_DEBUG_ASSERT(largest_interior_unused_ranges_.size() >= 1);
  std::optional<uint64_t> prev_end;
  for (auto iter = largest_interior_unused_ranges_.begin();
       iter != largest_interior_unused_ranges_.end(); prev_end = iter->end(), ++iter) {
    if (!prev_end) {
      continue;
    }
    // The gap_range in interior unused ranges is covering some used blocks, and possibly also some
    // un-used blocks that we need to cover just to keep the number of protection ranges within HW
    // limits.
    auto gap_range = Range::BeginEnd(*prev_end, iter->begin());
    goal_ranges_.emplace(std::move(gap_range));
  }

  // There may be gaps in coalesced_required_ranges_ that are being covered by these ranges, but
  // those gaps were not among the largest gaps, so we do intentionally want to cover them.
  auto left_range = Range::BeginEnd(coalesced_required_ranges_.begin()->begin(),
                                    largest_interior_unused_ranges_.begin()->begin());
  auto right_range = Range::BeginEnd(largest_interior_unused_ranges_.rbegin()->end(),
                                     coalesced_required_ranges_.rbegin()->end());
  goal_ranges_.emplace(std::move(left_range));
  goal_ranges_.emplace(std::move(right_range));
}

bool ProtectedRanges::DoOpAddRange(const Range& new_range_param) {
  auto new_range = new_range_param.Clone();
  if (!ranges_control_->UseRange(new_range)) {
    return false;
  }
  ranges_control_->AddProtectedRange(new_range);

  ranges_.emplace(std::move(new_range));
  ranges_bytes_ += new_range_param.length();

  TryCoalesceAdjacentRangesAt(&ranges_, new_range_param.begin());
  TryCoalesceAdjacentRangesAt(&ranges_, new_range_param.end());
  return true;
}

void ProtectedRanges::DoOpDelRange(const Range& old_range) {
  ZX_DEBUG_ASSERT(ranges_.find(old_range) != ranges_.end());
  ranges_control_->DelProtectedRange(old_range);

  ranges_bytes_ -= old_range.length();
  auto stash_old_range = old_range.Clone();
  ranges_.erase(old_range);

  ranges_control_->UnUseRange(stash_old_range);

  // No need to call TryCoalesceAdjacentRangesAt(ranges_, ...) here since this is a delete.
}

void ProtectedRanges::DoOpShortenRange(const Range& old_range, const Range& new_range_param) {
  // The caller can ask for a shorten where new_range is still in active use (ongoing protected DMA
  // to/from new_range), and it's this method's job to accomplish the shorten without disrupting any
  // DMA to/from new_range.
  //
  // This method must ensure that the calls made to ranges_control_ must not cause the TEE to need
  // to zero any part of any range that overlaps new_range, since the TEE would be allowed to
  // disrupt ongoing DMA to/from that overlapping portion of new_range, which we don't want.
  //
  // We avoid disrupting ongoing DMA to/from new_range by creating a temp_range which covers the
  // portion of old_range that's going away, so that we can delay any zeroing by the TEE until the
  // last ranges_control_ step below, at which point there's no longer any other range overlapping
  // temp_range, so only DMA to/from temp_range can be disrupted (not DMA to/from any part of
  // new_range).
  //
  // This method is only called when we already know that there isn't any ongoing DMA to/from
  // temp_range.
  Range new_range = new_range_param.Clone();
  ZX_DEBUG_ASSERT(ranges_.find(old_range) != ranges_.end());
  ZX_DEBUG_ASSERT(!old_range.empty());
  ZX_DEBUG_ASSERT(!new_range.empty());
  ZX_DEBUG_ASSERT(old_range.begin() == new_range.begin() || old_range.end() == new_range.end());
  ZX_DEBUG_ASSERT(new_range.length() < old_range.length());
  ZX_DEBUG_ASSERT(ranges_.size() <= max_logical_range_count_);
  ZX_DEBUG_ASSERT(max_range_count_ - max_logical_range_count_ >= (is_mod_available_ ? 1 : 2));

  Range temp_range;
  if (old_range.begin() == new_range.begin()) {
    temp_range = Range::BeginEnd(new_range.end(), old_range.end());
  } else {
    temp_range = Range::BeginEnd(old_range.begin(), new_range.begin());
  }
  ranges_control_->AddProtectedRange(temp_range);
  if (is_mod_available_) {
    ranges_control_->ModProtectedRange(old_range, new_range);
  } else {
    ranges_control_->AddProtectedRange(new_range);
    ranges_control_->DelProtectedRange(old_range);
  }
  ranges_control_->DelProtectedRange(temp_range);

  ranges_bytes_ -= old_range.length();
  ranges_.erase(old_range);
  ranges_bytes_ += new_range.length();
  ranges_.emplace(std::move(new_range));

  ranges_control_->UnUseRange(temp_range);
}

bool ProtectedRanges::DoOpMergeRanges(const Range& first_range, const Range& second_range) {
  ZX_DEBUG_ASSERT(!first_range.empty());
  ZX_DEBUG_ASSERT(!second_range.empty());
  // We never create two ranges in ranges_ without immediately coalescing them back to one range via
  // TryCoalesceAdjacentRangesAt(ranges_, ...), so DoOpMergeRanges() is only ever used if there's at
  // least a one block gap between first_range and second_range.
  ZX_DEBUG_ASSERT(first_range.end() < second_range.begin());
  ZX_DEBUG_ASSERT(max_range_count_ - max_logical_range_count_ >= (is_mod_available_ ? 0 : 1));

  const Range gap_going_away = Range::BeginEnd(first_range.end(), second_range.begin());
  if (!ranges_control_->UseRange(gap_going_away)) {
    return false;
  }

  Range new_range = Range::BeginEnd(first_range.begin(), second_range.end());
  if (is_mod_available_) {
    ranges_control_->ModProtectedRange(first_range, new_range);
    ranges_control_->DelProtectedRange(second_range);
  } else {
    ranges_control_->AddProtectedRange(new_range);
    ranges_control_->DelProtectedRange(first_range);
    ranges_control_->DelProtectedRange(second_range);
  }

  ranges_bytes_ -= first_range.length();
  ranges_.erase(first_range);
  ranges_bytes_ -= second_range.length();
  ranges_.erase(second_range);
  ranges_bytes_ += new_range.length();
  ranges_.emplace(std::move(new_range));

  return true;
}

void ProtectedRanges::DoOpSplitRange(const Range& range_to_split,
                                     const Range& new_gap_to_stop_using) {
  ZX_DEBUG_ASSERT(!range_to_split.empty());
  ZX_DEBUG_ASSERT(!new_gap_to_stop_using.empty());
  ZX_DEBUG_ASSERT(range_to_split.begin() < new_gap_to_stop_using.begin());
  ZX_DEBUG_ASSERT(new_gap_to_stop_using.end() < range_to_split.end());
  ZX_DEBUG_ASSERT(ranges_.size() <= max_logical_range_count_ - 1);
  ZX_DEBUG_ASSERT(max_range_count_ - max_logical_range_count_ >= (is_mod_available_ ? 1 : 2));

  Range new_left_range = Range::BeginEnd(range_to_split.begin(), new_gap_to_stop_using.begin());
  Range new_right_range = Range::BeginEnd(new_gap_to_stop_using.end(), range_to_split.end());

  ranges_control_->AddProtectedRange(new_left_range);
  ranges_control_->AddProtectedRange(new_gap_to_stop_using);
  if (is_mod_available_) {
    ranges_control_->ModProtectedRange(range_to_split, new_right_range);
  } else {
    ranges_control_->AddProtectedRange(new_right_range);
    ranges_control_->DelProtectedRange(range_to_split);
  }
  ranges_control_->DelProtectedRange(new_gap_to_stop_using);

  ranges_bytes_ -= range_to_split.length();
  ranges_.erase(range_to_split);
  ranges_bytes_ += new_left_range.length();
  ranges_.emplace(std::move(new_left_range));
  ranges_bytes_ += new_right_range.length();
  ranges_.emplace(std::move(new_right_range));

  ranges_control_->UnUseRange(new_gap_to_stop_using);
}

bool ProtectedRanges::DoOpExtendRangeEnd(const Range& old_range, const Range& new_range_param) {
  auto new_range = new_range_param.Clone();
  ZX_DEBUG_ASSERT(old_range.begin() == new_range.begin());
  ZX_DEBUG_ASSERT(new_range.end() > old_range.end());
  if (!ranges_control_->UseRange(Range::BeginEnd(old_range.end(), new_range.end()))) {
    return false;
  }
  // Since we're only extending the end of a range, we can get away with just adding the new range
  // and deleting the old range, since the deletion of the old range will not need to zero any
  // blocks, so the TEE won't need to change the old_range per-device DMA write permissions.
  //
  // Using add/del here instead of mod only uses 1 transient range which is <= 2, so no reason to
  // use mod here.
  ranges_control_->AddProtectedRange(new_range);
  ranges_control_->DelProtectedRange(old_range);

  ranges_bytes_ -= old_range.length();
  ranges_.erase(old_range);
  ranges_bytes_ += new_range.length();
  auto new_range_end = new_range.end();
  ranges_.emplace(std::move(new_range));

  TryCoalesceAdjacentRangesAt(&ranges_, new_range_end);

  return true;
}

void ProtectedRanges::TryCoalesceAdjacentRangesAt(Ranges* ranges, uint64_t location) {
  ZX_DEBUG_ASSERT(ranges);
  // Determine if we actually have two adjacent barely-touching blocks that touch at location.
  if (ranges->size() < 2) {
    return;
  }
  auto second_block_iter = ranges->lower_bound(Range::BeginLength(location, 0));
  if (second_block_iter == ranges->end()) {
    // There's no block with begin() == location, so no suitable pair of blocks to weld at location.
    return;
  }
  if (second_block_iter->begin() != location) {
    ZX_DEBUG_ASSERT(second_block_iter->begin() > location);
    // There's no block starting at exactly location, so there can be no pair of blocks
    // barely-touching at location.
    return;
  }
  if (second_block_iter == ranges->begin()) {
    // No block before second_block_iter, so no welding at location is possible.
    return;
  }
  auto first_block_iter = second_block_iter;
  --first_block_iter;
  if (first_block_iter->end() != location) {
    // There's a gap, so no welding at location is possible.
    ZX_DEBUG_ASSERT(first_block_iter->end() < location);
    return;
  }
  ZX_DEBUG_ASSERT(first_block_iter->end() == location);
  ZX_DEBUG_ASSERT(second_block_iter->begin() == location);
  Range new_range = Range::BeginEnd(first_block_iter->begin(), second_block_iter->end());
  if (ranges == &ranges_) {
    // Really do the coalesce, since ranges_ mirrors HW, so keep HW up to date.
    //
    // Welding two blocks is possible.
    //
    // Since we use this in the AddRange() path and may already be at max_logical_ranges_ + 1, we
    // need to use mod if available.
    if (is_mod_available_) {
      ranges_control_->ModProtectedRange(*first_block_iter, new_range);
      ranges_control_->DelProtectedRange(*second_block_iter);
    } else {
      ranges_control_->AddProtectedRange(new_range);
      ranges_control_->DelProtectedRange(*first_block_iter);
      ranges_control_->DelProtectedRange(*second_block_iter);
    }
  }
  ranges->erase(first_block_iter);
  ranges->erase(second_block_iter);
  ranges->emplace(std::move(new_range));
}

// un-covered pages / un-used pages
double ProtectedRanges::GetEfficiency() {
  // un-covered bytes
  uint64_t un_covered_bytes = ranges_control_->GetSize() - ranges_bytes_;

  // un-used bytes
  uint64_t un_used_bytes = ranges_control_->GetSize() - requested_bytes_;

  return static_cast<double>(un_covered_bytes) / static_cast<double>(un_used_bytes);
}

// un-covered pages / total pages
double ProtectedRanges::GetLoanedRatio() {
  // un-covered bytes
  uint64_t un_covered_bytes = ranges_control_->GetSize() - ranges_bytes_;

  // total bytes
  uint64_t total_bytes = ranges_control_->GetSize();

  return static_cast<double>(un_covered_bytes) / static_cast<double>(total_bytes);
}

bool Range::IsOverlap(const Range& a, const Range& b) {
  if (a.end() <= b.begin()) {
    return false;
  }
  if (b.end() <= a.begin()) {
    return false;
  }
  return true;
}

Range Range::Intersect(const Range& a, const Range& b) {
  // Caller should check if we need to intersect before calling intersect, just to avoid building
  // stuff we won't use.  Alternately we could relax this and return an arbitrary empty range.
  ZX_DEBUG_ASSERT(Range::IsOverlap(a, b));
  // This is intended to work for a ranges that may include the last block before a uint64_t wraps.
  // Note that I did not say that this won't wrap in that situation; this is meant to get the
  // correct answer by exploiting the wrapping (which isn't UB for uint64_t).
  uint64_t new_begin = std::max(a.begin(), b.begin());
  uint64_t new_last = std::min(a.end() - 1, b.end() - 1);
  uint64_t new_end = new_last + 1;
  uint64_t new_length = new_end - new_begin;
  return Range::BeginLength(new_begin, new_length);
}

}  // namespace protected_ranges
