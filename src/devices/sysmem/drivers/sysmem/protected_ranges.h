// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_PROTECTED_RANGES_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_PROTECTED_RANGES_H_

#include <inttypes.h>
#include <lib/fit/function.h>
#include <lib/zx/pmt.h>
#include <zircon/assert.h>

#include <set>

#include "macros.h"

// The classes in this namespace are for managing a limited set of HW protection ranges, while
// ensuring:
//  * continuous protection of in-use buffers
//  * as many pages are un-protected as possible, so we can decommit those pages
//  * HW protection range modification rules are followed:
//    * when deleting or shortening a range, the range must not overlap any in-use buffer unless
//      the entire range or portion of a range being deleted/shortened is also covered by other
//      protection ranges
//    * when zeroing a sub-range, the sub-range must be fully covered by an existing protection
//      range and not overlapping any other protection range
//    * we don't re-optimize the protection ranges all at once; instead we do so incrementally to
//      avoid churning loaned pages too fast for Zircon to keep up (in terms of borrowing
//      newly-loaned pages to make up for pages reclaimed / un-loaned / committed)
namespace protected_ranges {

class ProtectedRanges;

// This class represents a single protected range.
//
// The begin_ and length_ are immutable (const) from when the Range is created, but the pmt_ is
// mutable.
class Range {
 public:
  Range() = default;
  ~Range() { ZX_DEBUG_ASSERT(!pmt_); }
  // move-only
  Range(Range&& to_move) = default;
  Range& operator=(Range&& to_move) = default;
  // no implicit copy
  Range(const Range& to_copy) = delete;
  Range& operator=(const Range& to_copy) = delete;
  // explicit Clone(), as long as pmt_ is not set since we shouldn't clone that field
  Range Clone() const {
    CheckNotMovedOut();
    ZX_DEBUG_ASSERT(!pmt_);
    return Range{begin_, length_};
  }

  static Range BeginLength(uint64_t begin, uint64_t length) { return Range{begin, length}; }
  static Range BeginEnd(uint64_t begin, uint64_t end) { return Range{begin, end - begin}; }

  uint64_t begin() const {
    CheckNotMovedOut();
    return begin_;
  }
  uint64_t end() const {
    CheckNotMovedOut();
    return begin_ + length_;
  }
  uint64_t length() const {
    CheckNotMovedOut();
    return length_;
  }

  bool empty() const {
    CheckNotMovedOut();
    return end() <= begin();
  }

  // We have to define this because we want to use
  // std::multiset<Range, CompareRangeByBegin>::operator==() which does _not_ use
  // CompareRangeByBegin (unless we call CompareRangeByBegin within this operator==()).
  bool operator==(const Range& rhs) const;

  static bool IsOverlap(const Range& a, const Range& b);

  static Range Intersect(const Range& a, const Range& b);

  void SetMutablePmt(zx::pmt pmt) const {
    CheckNotMovedOut();
    ZX_DEBUG_ASSERT(!pmt_);
    pmt_ = std::move(pmt);
  }
  zx::pmt TakeMutablePmt() const {
    CheckNotMovedOut();
    // May or may not have a handle depending on whether client called SetPmt().
    return std::move(pmt_);
  }

 private:
  friend class CompareRangeByBegin;
  friend class CompareRangeByLength;

  class MoveTracker {
   public:
    MoveTracker() = default;
    MoveTracker(MoveTracker&& to_move) noexcept {
      ZX_DEBUG_ASSERT(!to_move.moved_out_);
      moved_out_ = to_move.moved_out_;
      to_move.moved_out_ = true;
    }
    MoveTracker& operator=(MoveTracker&& to_move) noexcept {
      ZX_DEBUG_ASSERT(!to_move.moved_out_);
      moved_out_ = to_move.moved_out_;
      to_move.moved_out_ = true;
      return *this;
    }
    void Check() const { ZX_DEBUG_ASSERT(!moved_out_); }

   private:
    bool moved_out_ = false;
  };

  explicit Range(uint64_t begin, uint64_t length) : begin_(begin), length_(length) {}
  void CheckNotMovedOut() const { move_tracker_.Check(); }

  uint64_t begin_ = 0;

  // We may as well have the primary representation use length instead of end, since it's convenient
  // that regardless of whether the address space has all-0xFF as a valid address, a length of 0 is
  // unambiguously less than the length of any real range (since zero-length ranges aren't "real" in
  // this code) and a length of all-0xFF is unambiguously greater than the length of any real memory
  // address space range since a length that large wouldn't leave room for anything else, like room
  // for this code to have this length_ field.
  //
  // In contrast, an end value of 0 could be the "real" end of the block whose last byte is at
  // std::numeric_limits<uint64_t>::max() where the end value was forced to wrap, and even an
  // all-0xFF end could be "real" if the block size is 1 byte (or a very large value of end could
  // still be real if the block size isn't 1 byte).
  //
  // The choice to have length_ here instead of end_ here should _not_ be taken as a claim that the
  // code currently handles all-0xFF being a valid address.  At least for now, this code does not
  // need to handle that situation, and is not tested to handle that situation, and will not
  // encounter that situation.
  uint64_t length_ = 0;

  // Each protected range must be pinned while protected; the actual pinning and un-pinning is the
  // client's responsibility (the implementation of ProtectedRangesControl must do the pinning and
  // un-pinning).  We track this here to avoid the client needing to maintain yet another set of
  // tracked ranges.
  mutable zx::pmt pmt_;

  MoveTracker move_tracker_;
};

class CompareRangeByBegin {
 public:
  bool operator()(const Range& left, const Range& right) const {
    left.CheckNotMovedOut();
    right.CheckNotMovedOut();
    if (left.begin() < right.begin()) {
      // <
      return true;
    }
    if (left.begin() > right.begin()) {
      // >
      return false;
    }

    if (left.length() < right.length()) {
      // <
      return true;
    }
    if (left.length() > right.length()) {
      // >
      return false;
    }

    // ==
    return false;
  }
};

inline bool Range::operator==(const Range& rhs) const {
  CheckNotMovedOut();
  rhs.CheckNotMovedOut();
  CompareRangeByBegin less;
  return !less(*this, rhs) && !less(rhs, *this);
}

class CompareRangeByLength {
 public:
  bool operator()(const Range& left, const Range& right) const {
    left.CheckNotMovedOut();
    right.CheckNotMovedOut();

    // Non-empty is required.
    ZX_DEBUG_ASSERT(left.begin() != left.end());
    ZX_DEBUG_ASSERT(right.begin() != right.end());

    if (left.length() < right.length()) {
      // <
      return true;
    }
    if (left.length() > right.length()) {
      // >
      return false;
    }

    if (left.begin() < right.begin()) {
      // <
      return true;
    }
    if (left.begin() > right.begin()) {
      // >
      return false;
    }

    ZX_DEBUG_ASSERT(left.end() == right.end());

    // ==
    return false;
  }
};

// Interface used by ProtectedRanges to query properties and control lower-layer HW ranges.  This
// interface is sub-classed by ProtectedRangesControl which adds on UseRange() and UnUseRange().
// The interfaces are separate because ProtectedRangesCoreControl and the methods added by
// ProtectedRangesControl are implemented at different layers, and it's convenient for the lower
// layer to only implement ProtectedRangesCoreControl not ProtectedRangesControl.
class ProtectedRangesCoreControl {
 public:
  // True means calls to DelRange() and ModRange() are allowed, and more than one call to AddRange()
  // is allowed.  False means only one call to AddRange() is allowed, and no other calls to
  // AddRange(), DelRange(), ModRage() will happen.
  virtual bool IsDynamic() = 0;
  virtual uint64_t MaxRangeCount() = 0;
  virtual uint64_t GetRangeGranularity() = 0;
  virtual bool HasModProtectedRange() = 0;

  // AddProtectedRange (required)
  //
  // If the system is too broken to add a range, ZX_PANIC() instead of returning.  A hard reboot
  // will result (after which all ranges are cleared).
  //
  // TODO(fxbug.dev/96061): When possible, configure sysmem to trigger reboot on driver remove.
  //
  // Add a range, which may overlap with existing ranges, but which will have a unique (begin, end).
  // By the time this returns, the new range is usable.  Any portions of this range which overlap
  // existing ranges must remain continuously usable during this call.
  //
  // Outside of tests, this will pin the range and HW-protect the range.
  virtual void AddProtectedRange(const Range& range) = 0;

  // DelProtectedRange (required)
  //
  // Delete a range uniquely identified by its unique (begin, end).  All portions of the range which
  // overlap with other extant ranges must remain continuously usable.
  //
  // This is not allowed to fail.  If the system is too broken to delete a protected range,
  // ZX_PANIC() instead of returning.  A hard reboot will result (after which all ranges are
  // cleared).
  //
  // TODO(fxbug.dev/96061): When possible, configure sysmem to trigger reboot on driver remove.
  //
  // It is acceptable for the entire range to become unusable during delete iff any portion of the
  // range is not covered by any other range(s).  This applies even if some of the range is also
  // covered by another range.  This is to permit range permissions to be restricted while at least
  // 1 byte of the range being deleted is being zeroed and the range is being deleted in HW.
  //
  // Outside of tests, this will HW-deprotect the range and un-pin.  Other ranges may still protect
  // some of the pages, in which case those pages will still have non-zero pin_count.
  virtual void DelProtectedRange(const Range& range) = 0;

  // ModProtectedRange
  //
  // If !HasModProtectedRange(), this won't get called ever and should not be overridden in the
  // sub-class.  If HasModProtectedRange(), this can get called and must be overridden in the
  // sub-class.
  //
  // Modify an old range to become a new range, identifying the old range by its unique
  // (begin, end).
  //
  // The modification will only ever modify one end of the range at a time.  In other words, either
  // old_range.begin() == new_range.begin(), or old_range.end() == new_range.end().
  //
  // If the system is too broken to modify a range, ZX_PANIC() instead of returning.  A hard reboot
  // will result (after which all ranges are cleared).
  //
  // TODO(fxbug.dev/96061): When possible, configure sysmem to trigger reboot on driver remove.
  //
  // If a range is being shortened, it is acceptable for the entire old range to become temporarily
  // unusable during the shortening iff any offsets no longer covered by this range are also not
  // covered by any other range.  This is to permit range permissions to be restricted while the
  // portion being removed from the range is being zeroed by the TEE and the range is being
  // shortened in HW.
  //
  // We only bother to use ModProtectedRange() (at this layer) when it makes the difference between
  // 2 transient ranges and 1 transient ranges.
  //
  // The aml-securemem layer automatically uses range modification for any range deletion, to ensure
  // that we never zero too much per call to the TEE.
  //
  // Outside of tests, this will pin the new range, modify the HW protection, and un-pin the old
  // range.
  virtual void ModProtectedRange(const Range& old_range, const Range& new_range) {
    // If this actually runs, it means the sub-class is returning true from HasModProtectedRange()
    // despite not overriding ModRange().
    ZX_PANIC("ModRange() not implemented by sub-class (despite HasModProtectedRange() true?)\n");
  }

  // Zero a sub-range of a current range.  The sub-range must be fully covered by exactly one
  // protected range, and not overlap with any other protected range.
  //
  // Zero the newly requested range using the TEE.  This way, any protected mode devices will see
  // the new buffer as filled with zeroes, instead of whatever REE-written zeroes might end up
  // looking like post-scramble.  In testing situations we pretend as if this is allowed at
  // arbitrary granularity, but in actual use (so far) this will assert that range is aligned at
  // page boundaries (partly because that's the smallest zeroing granularity that the TEE allows, by
  // design).
  //
  // We don't currently have the ability to temporarily de-protected a sub-range in order to zero
  // that sub-range outside the TEE.  As necessary we could add that.  However, that zeroing
  // wouldn't necessarily really be zeroing from the point of view of a device in protected mode
  // reading a page in the protected range due to some HW using a scramble.  That zeroing however
  // would avoid any possibility of bits from a different collection ending up effectively sent
  // downstream of a decoder, for example.  As long as we have actual zeroing of a protected
  // sub-range, let's just use that, since it's more rigorously actually logically zero and also
  // prevents any potential for mixing bits across collections.
  virtual void ZeroProtectedSubRange(bool is_covering_range_explicit, const Range& range) = 0;
};

// This is the virtual interface that (outside of tests) is a thin wrapper on top of
// fuchsia.sysmem.SecureMem (ProtectedRangesCoreControl) and Zircon contiguous VMO + page loaning +
// page reclaim (additional methods added by ProtectedRangesControl).  The only method of this
// interface that's allowed to fail is UseRange() since that can be expected to fail from Zircon
// under severe-enough memory pressure.  All other failures are treated as ZX_PANIC()-level fatal,
// which will result in a hard reboot.  Since any process including the sysmem driver can fail at
// any time due to system-wide memory overcommit, this ZX_PANIC() isn't any worse than that, and
// is only expected under similar circumstances.  The thinking is that failing bigger is actually
// better from the user's point of view once we've gotten to the point where something is using so
// much memory that small allocations and faulting in a page start failing; a reboot is overall
// better than getting stuck in that state.
//
// TODO(fxbug.dev/96061): When possible, configure sysmem to trigger reboot on driver remove.
class ProtectedRangesControl : public ProtectedRangesCoreControl {
 public:
  // Lowest begin() possible for any requested range.
  virtual uint64_t GetBase() = 0;
  // GetBase() + GetSize() is the highest end() posisble for any requested range.
  virtual uint64_t GetSize() = 0;

  // UseRange
  //
  // This is called shortly before an offset becomes part of any range, and will not be called on a
  // given offset more than once without any intervening un_use_range that covers the same offset.
  // This information can be inferred by watching add_range and mod_range (including tracking of
  // temporarily overlapping ranges), but it's simpler overall to implement UseRange() and let
  // ProtectedRanges inform of all ranges that are changing from outside all ranges to inside at
  // least one range.
  //
  // This is allowed to fail by returning false.  This is so that Zircon can refuse to reclaim pages
  // if memory pressure is too severe.  If this happens, the range is still considered un-used, and
  // will not be added.
  virtual bool UseRange(const Range& range) = 0;

  // UnUseRange
  //
  // This can't fail.  If the system is too broken to UnUseRange(), then ZX_PANIC() instead.  A hard
  // reboot will result.
  //
  // TODO(fxbug.dev/96061): When possible, configure sysmem to trigger reboot on driver remove.
  //
  // This is called shortly after an offset stops being part of any protected range, and will not be
  // called on a given offset more than once without any intervening use_range that covers the same
  // offset.
  //
  // This information can be inferred by watching del_range and mod_range (including tracking of
  // temporarily overlapping ranges), but it's simpler overall to implement UnUseRange() and let
  // ProtectedRanges inform of all ranges that are changing from inside at least one range to
  // outside all ranges.
  virtual void UnUseRange(const Range& range) = 0;
};

// The algorithmic goals of this class can be summarized as:
//   * Incrementally change from range set A to range set B, by generating add, delete, modify
//     steps.
//   * Ensure that all offsets in the intersection of A and B remain continuously usable for
//     protected DMA during the steps (and not accessible by REE CPU).  Pages not in A or not in B
//     may not be usable.
//   * Never try to have more extant ranges than the set limit.
//   * Maximize (to the degree implemented) the minimum number of pages that are outside any range
//     during the overall A to B sequence.  In other words, make the worst-case moment during the
//     sequence of steps be only as bad as it needs to be in terms of how many bytes are under
//     ranges (more bytes under ranges is worse, since we can't loan those ranges back to the rest
//     of the system).
// To that end,
//   * We'll never delete or modify to remove any offsets from a range unless the offsets being
//     removed are fully covered by other currently-extant ranges, or the entire range being deleted
//     or modified has no overlap with any offsets in the intersection of A and B.  This is because
//     removing offsets from a range is allowed to make the entire range temporarily unusable
//     including offsets that overlap with other extant ranges, unless _all_ offsets being removed
//     are covered by another extant range.
//   * We'll prioritize removal of offsets from ranges over adding offsets to ranges, while staying
//     under the set range count limit.  If we're forced to add offsets to ranges to stay under the
//     range count limit and we have multiple options, we'll choose the option that minimizes the
//     number of additional offsets that are under ranges.  If we have multiple options for how to
//     remove offsets from ranges, we'll pick the option that maximizes the number of offsets that
//     we're removing from ranges.  When possible while staying under the range count limit, we
//     choose to remove offsets from ranges before we add offsets to ranges.
// The intent is for this class to (ideally) handle range set updates such that there is no need for
// a securemem driver to hold ranges in reserve to emulate steps requested by this class.  As of
// this comment, there are no known securemem drivers that need to hold ranges in reserve to emulate
// steps requested by this class.
//
// For now this is optimized for readability more than efficiency, but if we encounter HW with
// unlimited HW protection ranges, it may make sense to revisit the algorithm aspects.
//
// Checking that invariants are actually true is left to protected_ranges_test.cc and
// ProtectedRangesTest_MiniStress, so that we can avoid doing repeated time-consuming invariant
// checks in debug builds.  The ProtectedRangesTest_MiniStress test verifies that we maintain the
// invariants properly given many pseudo-random upper range requests.
class ProtectedRanges {
 public:
  using Ranges = std::multiset<Range, CompareRangeByBegin>;

  explicit ProtectedRanges(ProtectedRangesControl* ranges_control);
  ~ProtectedRanges();

  uint64_t max_logical_ranges();

  // This method attempts to add an additional range to the set of requested protected ranges.  The
  // requested protected ranges are the ranges that need to all be continuously covered by a limited
  // number of HW-supported ranges.
  //
  // If this returns true, the added range is now usable as a protected range.  If this returns
  // false, the range was not added and no attempt should be made to use the range as a protected
  // range.  Do not use ranges that happen to be protected due to being in the set of HW-backed
  // protected ranges at any given moment, as HW-backed protected ranges may be adjusted at any
  // time, and the implementation is free to cause DMA glitches in ranges that are not in the set
  // of required protected ranges.
  //
  // This method typically will succeed even if there are more ranges added via AddRange() than the
  // number of HW-backed ranges.  In this case, at least one of the HW-backed ranges is used to
  // cover more than one required range.  This can lead to "extra" pages in between required ranges
  // which are HW-protected despite not being used as protected ranges.  These pages are still
  // considered "used" in terms of ProtectedRangesControl.UseRange() and UnUseRange(), but must not
  // be used for protected DMA, as the implementation is free to disrupt protected DMA to/from any
  // such protected gap.
  //
  // During this call, outgoing callbacks to ranges_control _may_ be made to effect the change.  The
  // outgoing calls can in some cases be more numerous and change other ranges, as the HW-backed
  // ranges are being re-optimized to some extent during this call.
  //
  // To finish optimizing ranges, the caller should call StepTowardOptimalRanges() until it returns
  // true, typically with a timer delay in between calls to avoid churning loaned pages too fast.
  //
  // range - the range to add to the raw set of ranges that must be protected.
  bool AddRange(const Range& range);

  // This method removes a range from the set of required protected ranges.  See also AddRange().
  //
  // This method can't fail.  If the system is too broken to delete a required range, this method
  // will ZX_PANIC() instead of returning.  A hard reboot will result.
  //
  // During this call, outgoing callbacks to ranges_control _may_ be made to effect the change.  The
  // outgoing calls can in some cases be more numerous and change other ranges, as the HW-backed
  // ranges are being re-optimized to some extent during this call.
  //
  // To finish optimizing ranges, the caller should call StepTowardOptimalRanges() until it returns
  // true, typically with a timer delay in between calls to avoid churning loaned pages too fast.
  //
  // range - the range to remove from the raw set of ranges that must be protected.
  void DeleteRange(const Range& range);

  // When AddRange() or DeleteRange() is called, we don't instantly try to fix the ranges to be
  // completely optimal immediately, because optimizing can involve some reclaiming of pages and
  // loaning of different pages.  If we do all of that too quickly, the opportunistic borrowing
  // during PageQueues rotation / GC will not necessarily have enough time to soak up the
  // newly-loaned pages before an OOM is triggered.  We basically want to incrementally step toward
  // optimal instead of slamming the whole set of ranges into place all at once, especially when
  // the optimal set of ranges is changing to a quite different configuration.
  //
  // As we incrementally optimize, it's possible we'll end up triggering a PageQueues rotation / GC
  // sooner than would have happened otherwise, and that's fine/good, but we do want to give
  // PageQueues rotation enough time to borrow (or "re-borrow" if you like, from the point of view
  // of an offset of a pager-backed VMO) some pages before we perform another step toward optimal
  // ranges.
  //
  // Despite the delayed optimization of ranges in steps with some delay in between steps, a
  // a successful call to AddRange() is guaranteed to make the added range usable for protected DMA.
  //
  // Calling this "extra" times after this has returned true and before any more AddRange() or
  // DeleteRange() is permitted, but is also not necessary, and won't achieve any further
  // optimization (until called after the next AddRange() or DeleteRange() when there may be more
  // optimizing to do).
  //
  // This method guarantees that it'll eventually return true if called repeatedly without any more
  // calls to AddRange() or DeleteRange(), _if_ memory pressure is low enough to allow optimizing
  // ranges.  If memory pressure is too high to make progress for a while, this method will keep
  // returning true during that while.
  //
  // true - known done optimizing, until the next call to AddRange() or DeleteRange().
  // false - call again later to try to do more optimizing.
  bool StepTowardOptimalRanges();

  // For each of the following Ranges accessors, the return reference should not be retained beyond
  // the next AddRange() or DeleteRange().

  // requested ranges
  const Ranges& requested_ranges() const { return requested_ranges_; }

  // requested_ranges() processed to align to block boundaries
  const Ranges& required_ranges() const { return required_ranges_; }

  // required_ranges() processed to merge overlaps and barely-touching ranges
  const Ranges& coalesced_required_ranges() const { return coalesced_required_ranges_; }

  // coalesced_required_ranges() gaps
  const Ranges& interior_unused_ranges() const { return interior_unused_ranges_; }

  // interior_unused_ranges() processed to keep only the largest gaps
  const Ranges& largest_interior_unused_ranges() const { return largest_interior_unused_ranges_; }

  // largest_interior_unused_ranges() gaps plus the left-most and right-most ranges needed to cover
  // the rest of colesced_required_ranges()
  const Ranges& goal_ranges() const { return goal_ranges_; }

  // current ranges; when called within a ProtectedRangesControl method, this will reutnr the
  // "before" ranges.
  const Ranges& ranges() const { return ranges_; }

  // only for dumping the small ranges in unit tests
  void DebugDumpRangeForUnitTest(const Range& range, const char* info);
  void DebugDumpRangesForUnitTest(const Ranges& ranges, const char* info);
  void DebugDumpOffset(uint64_t offset, const char* info);
  void DebugDumpBacktrace();
  void DynamicSetDlogEnabled(bool enabled);

  template <typename F1>
  void ForUnprotectedRanges(F1 callback) {
    const auto range = Range::BeginLength(ranges_control_->GetBase(), ranges_control_->GetSize());
    ForUnprotectedRangesOverlappingRange(range, std::move(callback));
  }

  template <typename F1>
  void ForUnprotectedRangesOverlappingRange(const Range& range, F1 callback) {
    const auto entire_range =
        Range::BeginLength(ranges_control_->GetBase(), ranges_control_->GetSize());
    if (!is_dynamic_) {
      // If !is_dynamic_, there are no logically unprotected ranges.
      return;
    }
    if (ranges_.empty()) {
      callback(Range::Intersect(entire_range, range));
      return;
    }
    std::optional<uint64_t> prev_end;
    auto [iter_begin, iter_end] =
        IteratorsCoveringPotentialOverlapsOfRangeWithRanges(range, ranges_);
    // Back up by one range in case there's a gap before that overlaps range.
    if (iter_begin != ranges_.begin()) {
      --iter_begin;
    }
    // Advance by one range in case there's a gap after that overlaps range.
    if (iter_end != ranges_.end()) {
      ++iter_end;
    }
    ZX_DEBUG_ASSERT(iter_begin != iter_end);
    // We need both interior and exterior gaps.
    //
    // Check for overlapping exterior gap at the beginning.
    if (iter_begin == ranges_.begin()) {
      const auto first_gap = Range::BeginEnd(entire_range.begin(), iter_begin->begin());
      if (Range::IsOverlap(first_gap, range)) {
        callback(Range::Intersect(first_gap, range));
      }
    }
    // Handle all the interior gaps.
    for (auto iter = iter_begin; iter != iter_end; prev_end = iter->end(), ++iter) {
      if (!prev_end) {
        continue;
      }
      const auto gap = Range::BeginEnd(*prev_end, iter->begin());
      if (Range::IsOverlap(gap, range)) {
        callback(Range::Intersect(gap, range));
      }
    }
    ZX_DEBUG_ASSERT(prev_end);
    // Check for overlapping exterior gap at the end.
    if (iter_end == ranges_.end()) {
      const auto last_gap = Range::BeginEnd(ranges_.rbegin()->end(), entire_range.end());
      if (Range::IsOverlap(last_gap, range)) {
        callback(Range::Intersect(last_gap, range));
      }
    }
  }

  // Requirement: Either ranges must not contain any self-overlaps (all Ranges available via this
  // class except required_ranges()), or ranges must be required_ranges().  The required_ranges()
  // can have a limited degree of self-overlap, which this method does accommodate.
  //
  // The returned iterators are a begin, end pair.  If there is no range in "ranges" that overlaps
  // "range", then both iterators will be ranges.end().  If there are any ranges in "ranges" that
  // overlap "range", then [begin, end) is exactly those ranges in "ranges" which overlap "range".
  static std::pair<Ranges::iterator, Ranges::iterator>
  IteratorsCoveringPotentialOverlapsOfRangeWithRanges(const Range& range, const Ranges& ranges);

  // un-covered pages / un-used pages
  //
  // 1.0 - all un-used pages are un-covered so they can be loaned.
  // 0.0 - zero un-used pages are un-covered; no loaning of unused pages can happen.
  //
  // If there are zero unused pages, all pages un-coverd returns 1.0, and any pages covered
  // returns 0.0.
  double GetEfficiency();

  // un-covered pages / total pages
  //
  // 1.0 - all protected pages loaned
  // 0.0 - no protected pages loaned
  double GetLoanedRatio();

 private:
  using RangesByLength = std::multiset<Range, CompareRangeByLength>;

  // This is the core routine that is called from StepTowardOptimalRanges() with allow_userange true
  // and to fix up after DeleteRange() with allow_userange false.
  bool StepTowardOptimalRangesInternal(bool allow_userange);

  // Uses required_ranges_ to incrementally update coalesced_required_ranges_ and
  // interior_unused_ranges_.  Re-builds largest_interior_unused_ranges_ and goal_ranges_.
  // This just calls these methods in sequence:
  //  * UpdateCoalescedRequiredRanges()
  //  * UpdateInteriorUnusedRanges()
  //  * BuildLargestInteriorUnusedRanges()
  //  * BuildGoalRanges()
  void PropagateRequiredRangesToGoalRanges(const Range& diff_range);

  // Incrementally update coalesced_required_ranges_ from overlaps-allowed required_ranges_.
  // The caller promises that in required_ranges_ the protected/non-protected status of aligned
  // blocks outside of diff_range has not changed.
  void UpdateCoalescedRequiredRanges(const Range& diff_range);

  // Incrementally update interior_unused_ranges_ and interior_unused_ranges_by_length_ from
  // coalesced_required_ranges_.  The caller promises that in coalesced_required_ranges_ the
  // protected/non-protected status of aligned blocks outside of diff_range has not changed since
  // last time UpdateInteriorUnusedRanges() was called.
  void UpdateInteriorUnusedRanges(const Range& diff_range);

  // Clear and re-build largest_interior_unused_ranges_ from interior_unused_ranges_by_length_.
  void BuildLargestInteriorUnusedRanges();

  // Clear and re-build goal_ranges_ from largest_interior_unused_ranges_.
  void BuildGoalRanges();

  bool FixupRangesDuringAdd(const Range& new_required_range);
  void FixupRangesDuringDelete(const Range& old_required_range);

  // Each of the "DoOpX" methods below start with ranges_ non-overlapping and coalesced, and end
  // with ranges_ non-overlapping and coalesced.  Within a DoOpX(), ranges_ can overlap and not
  // be coalesced.  For each DoOpX() that returns a bool, false means failure with no changes made,
  // and true means success including re-establishing ranges_ as non-overlapping and coalesced.

  // The new_range must not overlap with any range in ranges_ on entry, but it may be immediately
  // adjacent to a range already in ranges_.
  bool DoOpAddRange(const Range& new_range);

  // The old_range (exact match) must exist in ranges_ on entry.
  void DoOpDelRange(const Range& old_range);

  // Shorten at exactly one end.
  void DoOpShortenRange(const Range& old_range, const Range& new_range);

  // Merge two ranges separated only by a non-empty (non-zero-length) gap.  To merge two ranges with
  // no gap in between we use TryCoalesceAdjacentRangesAt() instead.
  bool DoOpMergeRanges(const Range& first_range, const Range& second_range);

  // Split a range into two ranges, with a new_gap_to_stop_using non-zer-length gap that must
  // initially be covered by the range_to_split and not touch the beginning or end of the range to
  // split.
  void DoOpSplitRange(const Range& range_to_split, const Range& new_gap_to_stop_using);

  // The old_range and new_range must not overlap any _other_ ranges in ranges_, but there must be
  // overlap between old_range and new_range.  The old_range.begin() must equal new_range.begin().
  bool DoOpExtendRangeEnd(const Range& old_range, const Range& new_range);

  // TryCoalesceAdjacentRangesAt()
  //
  // This is "try" in the sense that there may not be two ranges that are adjacent with no gap
  // (barely) touching at location.  If two ranges are touching at location, they will be coalesced.
  //
  // On entry *ranges has no overlaps, but may have adjacent barely-touching ranges.  There _may_ be
  // a pair of ranges that barely touch at location.
  //
  // On return, if there were two ranges barely touching at location, those two ranges are logically
  // replaced with a single range that covers the entire range of blocks previously spanned by the
  // two ranges.  This method doesn't try to coalesce the new larger block with any further
  // adjacent blocks; welding of two blocks is only performed for the specific location passed in.
  void TryCoalesceAdjacentRangesAt(Ranges* ranges, uint64_t location);

  ProtectedRangesControl* ranges_control_ = nullptr;

  // Each of the RangeSet(s) below is either incrementally updated or limited in size to no more
  // than max_logical_ranges().

  // The set of ranges that the client of ProtectedRanges has requested be protected.  The limited
  // number of HW-backed ranges will be used to cover at least these ranges.
  //
  // The alignment of these ranges may not be range_granularity_.  Typically these ranges will be
  // page aligned while range_granularity_ is a larger power of 2 like 64 KiB.
  //
  // Overlap within this set of ranges is not allowed.
  Ranges requested_ranges_;
  uint64_t requested_bytes_ = 0;

  // These are requested_ranges_ but with each range aligned to range_granularity_.  Duplicates are
  // allowed because aligning two short nearby ranges can cause them to become duplicates.  We
  // expect consistency in the number of copies of a given short range with how many ranges in
  // requested_ranges_ will generate that range.  Overlaps are allowed because the end of a first
  // range gets rounded up and the beginning of a next range gets rounded down.  However, these
  // overlaps are not arbitrary overlap in the sense that there's only a maximum of 1 block of
  // overlap between any two ranges.  We exploit the fact that the overlaps aren't completely
  // arbitrary to avoid unnecessary generality in the range processing code.
  Ranges required_ranges_;

  // This is a non-overlapping and coalesced set of ranges derived from required_ranges_.  If an
  // aligned block is covered by any range in required_ranges_, the same aligned block will be
  // covered by exactly one range in non_overlapping_required_ranges_, else the block will not be
  // covered by any range in coalesced_required_ranges_.  This is updated based on required_ranges_
  // incrementally.  This incremental update exploits the fact that required_ranges_ has only highly
  // constrained duplicates and overlaps, not arbitrary duplicates and overlaps.
  Ranges coalesced_required_ranges_;

  // This is a non-overlapping and coalesced set of ranges which is the negative of the interior
  // portion of coalesced_required_ranges_.  This is updated based on coalesced_required_ranges_
  // incrementally.  Along with interior_unused_ranges_by_length_, this allows us to determine the
  // most optimal set of gaps (covering the most blocks) to have _between_ the limited number of
  // available HW-based protection ranges.  We only analyze the interior unused ranges because the
  // exterior unused ranges must exist regardless (we get those for free).  The exterior unused
  // ranges are dealt with in a later stage by including the blocks covered by the first and last
  // ranges in required_ranges_ when building goal_ranges_.
  Ranges interior_unused_ranges_;
  RangesByLength interior_unused_ranges_by_length_;

  // This is the current goal set of interior gaps.  We keep this as a member variable for tests.
  // This is the max_logical_ranges() max-sized ranges in interior_unused_ranges_by_length_, sorted
  // by begin() instead of length.  We do not update this incrementally, but its size is limited to
  // max_logical_ranges() - 1.
  Ranges largest_interior_unused_ranges_;

  // This is the current goal set of ranges.  This is what ranges_ "should" be asap to loan the max
  // number of pages back to Zircon.  However, to avoid churning pages from/to Zircon, the ranges_
  // can lag behind goal_ranges_.  StepTowardOptimalRanges() is called to get ranges_ one "step"
  // closer to goal_ranges_.  This is the negative of largest_interior_unused_ranges_, expanded to
  // include the first and last range in coalesced_required_ranges_.  We don't update this
  // incrementally, but its size is limited to max_logical_ranges().
  Ranges goal_ranges_;

  // Current state of protection ranges that have been set via ranges_control_.  During a call out
  // via ranges_control_, this is the pre-modification set of ranges.  This is updated incrementally
  // and it's size is limited to no more than max_logical_ranges() + 3 (or +2 if
  // HasModProtectedRange()).
  //
  // If warm reboot is needed, the secmem driver is responsible for deleting all protection ranges
  // immediately before the warm reboot (at least for now).  While that mechanism will delete the
  // same set of ranges as ranges_ has, that mechanism is entirely in aml-securemem and not reliant
  // on ranges_ here.
  Ranges ranges_;
  uint64_t ranges_bytes_ = 0;

  // If false, only a single call to AddRange() is allowed.  Even if ProtectedRanges is deleted,
  // the single added range is not deleted.  Immediately prior to a warm reboot, the secmem driver
  // itself will remove the single added range.
  bool is_dynamic_ = false;

  // This (absolute) max applies to ranges_ (at all times), not to required_ranges_.
  uint64_t max_range_count_ = 0;

  // If true, we can use ModProtectedRange().  If false, ModProtectedRange() can't be used and will
  // likely ZX_PANIC() if called.
  bool is_mod_available_ = false;

  // This (logical) max applies to ranges_ (while outside transient transitions), not to
  // required_ranges_.
  uint64_t max_logical_range_count_ = 0;

  // The alignment requirement for ranges.
  uint64_t range_granularity_ = 0;
};

}  // namespace protected_ranges

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_PROTECTED_RANGES_H_
