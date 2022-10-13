// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_VERSIONING_TYPES_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_VERSIONING_TYPES_H_

#include <zircon/assert.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>

namespace fidl {

// This file defines types used for FIDL Versioning. For more detail, read
// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0083_fidl_versioning#formalism.

// A platform represents a group of FIDL libraries that are versioned together.
// Usually all the library names begin with a common prefix, the platform name.
class Platform final {
 public:
  // Returns a platform if `str` is a valid platform identifier.
  static std::optional<Platform> Parse(std::string str);

  const std::string& name() const { return name_; }

  constexpr bool operator==(const Platform& rhs) const { return name_ == rhs.name_; }
  constexpr bool operator!=(const Platform& rhs) const { return name_ != rhs.name_; }

  struct Compare {
    bool operator()(const Platform& lhs, const Platform& rhs) const {
      return lhs.name_ < rhs.name_;
    }
  };

 private:
  explicit Platform(std::string name) : name_(std::move(name)) {}

  std::string name_;
};

// A version represents a particular state of a platform.
//
// Versions are categorized like so:
//
//     Finite
//         Numeric -- 1, 2, ..., 2^63-1
//         HEAD    -- the unstable, most up-to-date version
//         LEGACY  -- HEAD plus legacy elements
//     Infinite
//         -inf    -- the infinite past
//         +inf    -- the infinite future
//
// Infinite versions help avoid special cases in algorithms. For example, in a
// FIDL library that has no @available attributes at all, everything is
// considered added at HEAD and removed at +inf.
//
// A finite version's ordinal is the uint64 format specified in RFC-0083:
//
//               { numeric versions }                       HEAD  LEGACY
//        o------o------o--- ... ---o------o--- ... ---o------o------o
//        0      1      2        2^63-1   2^63     2^64-3  2^64-2  2^64-1
//
// Internally, this class uses a different format to represent -inf and +inf:
//
//      -inf     { numeric versions }                HEAD  LEGACY  +inf
//        o------o------o--- ... ---o------o--- ... ---o------o------o
//        0      1      2        2^63-1   2^63     2^64-2   2^64-1
//
// Note that HEAD and LEGACY are bumped down to make comparisons work properly.
class Version final {
 public:
  // Succeeds if `ordinal` corresponds to a finite version.
  static std::optional<Version> From(uint64_t ordinal);
  // Succeeds if `str` can be parsed as a numeric version, or is "HEAD" or "LEGACY".
  static std::optional<Version> Parse(std::string_view str);

  // Special version before all others. "Added at -inf" means "no beginning".
  //
  // TODO(fxbug.dev/67858): Originally this was used for "unversioned"
  // libraries, where everything was available on (-inf, +inf). We decided to
  // eliminate the concept of an "unversioned" library, defaulting instead to
  // the availability [HEAD, +inf). Now NegInf is not really needed, and PosInf
  // should perhaps be renamed to "Inf".
  static constexpr Version NegInf() { return Version(0); }
  // Special version after all others. "Removed at +inf" means "no end".
  static constexpr Version PosInf() { return Version(std::numeric_limits<uint64_t>::max()); }
  // Special version meaning "the unstable, most up-to-date version".
  static constexpr Version Head() { return Version(std::numeric_limits<uint64_t>::max() - 2); }
  // Special version that is like HEAD but includes legacy elements.
  static constexpr Version Legacy() { return Version(std::numeric_limits<uint64_t>::max() - 1); }

  // Returns the version's ordinal. Assumes the version is finite.
  uint64_t ordinal() const;
  // Returns a string representation of the version.
  std::string ToString() const;

  constexpr bool operator==(const Version& rhs) const { return value_ == rhs.value_; }
  constexpr bool operator!=(const Version& rhs) const { return value_ != rhs.value_; }
  constexpr bool operator<(const Version& rhs) const { return value_ < rhs.value_; }
  constexpr bool operator<=(const Version& rhs) const { return value_ <= rhs.value_; }
  constexpr bool operator>(const Version& rhs) const { return value_ > rhs.value_; }
  constexpr bool operator>=(const Version& rhs) const { return value_ >= rhs.value_; }

 private:
  constexpr explicit Version(uint64_t value) : value_(value) {}

  uint64_t value_;
};

// A version range is a nonempty set of versions in some platform, from an
// inclusive lower bound to an exclusive upper bound.
class VersionRange final {
 public:
  constexpr explicit VersionRange(Version lower, Version upper_exclusive)
      : pair_(lower, upper_exclusive) {
    ZX_ASSERT_MSG(lower < upper_exclusive, "invalid version range");
  }

  // Returns the [lower, upper) version pair.
  const std::pair<Version, Version>& pair() const { return pair_; }

  // Returns true if this range contains `version`.
  bool Contains(Version version) const;

  // Returns the intersection of two (possibly empty) ranges.
  static std::optional<VersionRange> Intersect(const std::optional<VersionRange>& lhs,
                                               const std::optional<VersionRange>& rhs);

  constexpr bool operator==(const VersionRange& rhs) const { return pair_ == rhs.pair_; }
  constexpr bool operator!=(const VersionRange& rhs) const { return pair_ != rhs.pair_; }
  constexpr bool operator<(const VersionRange& rhs) const { return pair_ < rhs.pair_; }
  constexpr bool operator<=(const VersionRange& rhs) const { return pair_ <= rhs.pair_; }
  constexpr bool operator>(const VersionRange& rhs) const { return pair_ > rhs.pair_; }
  constexpr bool operator>=(const VersionRange& rhs) const { return pair_ >= rhs.pair_; }

 private:
  std::pair<Version, Version> pair_;
};

// A version set is a nonempty set of versions in some platform, made of either
// one range or two disjoint ranges.
class VersionSet final {
 public:
  constexpr explicit VersionSet(VersionRange first,
                                std::optional<VersionRange> second = std::nullopt)
      : ranges_(first, second) {
    ZX_ASSERT_MSG(VersionRange::Intersect(first, second) == std::nullopt,
                  "ranges must be disjoint");
    if (second) {
      auto [a1, b1] = first.pair();
      auto [a2, b2] = second.value().pair();
      ZX_ASSERT_MSG(b1 < a2, "ranges must be in order and noncontiguous");
    }
  }

  // Returns the first range and optional second range.
  const std::pair<VersionRange, std::optional<VersionRange>>& ranges() const { return ranges_; }

  // Returns true if this set contains `version`.
  bool Contains(Version version) const;

  // Returns the intersection of two (possibly empty) sets. The result must be
  // expressible as a VersionSet, i.e. not more than 2 pieces.
  static std::optional<VersionSet> Intersect(const std::optional<VersionSet>& lhs,
                                             const std::optional<VersionSet>& rhs);

  constexpr bool operator==(const VersionSet& rhs) const { return ranges_ == rhs.ranges_; }
  constexpr bool operator!=(const VersionSet& rhs) const { return ranges_ != rhs.ranges_; }
  constexpr bool operator<(const VersionSet& rhs) const { return ranges_ < rhs.ranges_; }
  constexpr bool operator<=(const VersionSet& rhs) const { return ranges_ <= rhs.ranges_; }
  constexpr bool operator>(const VersionSet& rhs) const { return ranges_ > rhs.ranges_; }
  constexpr bool operator>=(const VersionSet& rhs) const { return ranges_ >= rhs.ranges_; }

 private:
  std::pair<VersionRange, std::optional<VersionRange>> ranges_;
};

// An availability represents the versions when a FIDL element was added (A),
// deprecated (D), removed (R), and re-added as legacy (L) in a platform. These
// versions break the platform's timeline into the following regions:
//
//     Present        -- [A, R) and [L, +inf) if L is set
//         Available  -- [A, D or R)
//         Deprecated -- [D, R) if D is set
//         Legacy     -- [L, +inf) if L is set
//     Absent         -- (-inf, A) and [R, L or +inf)
//
// Here is what the timeline looks like for finite versions A, D, R:
//
//   -inf         A-1  A              D-1  D              R-1  R         +inf
//     o--- ... ---o---o--- ....... ---o---o--- ....... ---o---o--- ... ---o
//     |           |   |-- Available --|   |-- Deprecated -|   |           |
//     |-- Absent -|   |-------------- Present ------------|   |-- Absent -|
//
// Here is what the timeline looks like for a legacy element (L = LEGACY):
//
//   -inf         A-1  A              R-1  R          L-1   L          +inf
//     o--- ... ---o---o--- ....... ---o---o--- ... ---o----o---- ... ---o
//     |           |   |-- Available --|   |           |    |-- Legacy --|
//     |-- Absent -|   |--- Present ---|   |-- Absent -|    |-- Present -|
//
// Here is what the timeline looks like for Availability::Unbounded():
//
//   -inf                                                                +inf
//     o-------------------------------------------------------------------o
//     |---------------------------- Available ----------------------------|
//     |----------------------------- Present -----------------------------|
//
class Availability final {
 public:
  constexpr explicit Availability() = default;

  // Returns an availability that exists forever.
  //
  // TODO(fxbug.dev/67858): Originally this was used for "unversioned"
  // libraries, where everything was available on (-inf, +inf). We decided to
  // eliminate the concept of an "unversioned" library, defaulting instead to
  // the availability [HEAD, +inf). Now the only purpose of Unbounded is to be a
  // base case for Inherit(), but we could possibly remove it and instead have a
  // way of constructing a root availability already in the kInherited state.
  static constexpr Availability Unbounded() {
    Availability unbounded(State::kInherited);
    unbounded.added_ = Version::NegInf();
    unbounded.removed_ = Version::PosInf();
    unbounded.legacy_ = Legacy::kNotApplicable;
    return unbounded;
  }

  // An availability advances through four states. All reach kNarrowed on
  // success, except for library availabilities, which stay at kInherited
  // because libraries do not get decomposed.
  enum class State {
    // 1. Default constructed. All fields are null.
    kUnset,
    // 2. `Init` succeeded. Some fields might be set, and they are in order.
    kInitialized,
    // 3. `Inherit` succeeded. Now `added`, `removed`, and `legacy` are always set.
    kInherited,
    // 4. `Narrow` succeeded. Now `deprecated` is unset or equal to `added`, and
    //    `legacy` is either kNotApplicable or kNo.
    kNarrowed,
    // One of the steps failed.
    kFailed,
  };

  State state() const { return state_; }

  // Returns the points demarcating the availability: `added`, `removed`,
  // `deprecated` (if deprecated), and LEGACY and +inf (if Legacy::kYes).
  // Must be in the kInherited or kNarrowed state.
  std::set<Version> points() const;

  // Returns the presence set: [added, removed) and possibly [LEGACY, +inf).
  // Must be in the kInherited or kNarrowed state.
  VersionSet set() const;

  // Returns the presence range: [added, removed). Must be in the kNarrowed state.
  VersionRange range() const;

  // Returns true if the whole range is deprecated, and false if none of it is.
  // Must be in the kNarrowed state (where deprecation is all-or-nothing).
  bool is_deprecated() const;

  // Explicitly mark the availability as failed. Must not have called Init yet.
  void Fail();

  // Represents whether an availability includes legacy support.
  enum class Legacy {
    // Not applicable because [added, removed) already includes LEGACY,
    // i.e. `removed` is +inf.
    kNotApplicable,
    // No legacy support: do not re-add at LEGACY.
    kNo,
    // Legacy support: re-add at LEGACY.
    kYes,
  };

  // Named arguments for Init.
  struct InitArgs {
    std::optional<Version> added, deprecated, removed;
    std::optional<Legacy> legacy;
  };

  // Must be called first. Initializes the availability from @available fields.
  // Returns false if they do not satisfy `added <= deprecated < removed`. If
  // `deprecated` is set, it must be finite.
  bool Init(InitArgs args);

  struct InheritResult {
    enum class Status {
      kOk,
      // Child {added, deprecated, or removed} < Parent added.
      kBeforeParentAdded,
      // Child deprecated > Parent deprecated.
      kAfterParentDeprecated,
      // Child {added or deprecated} >= Parent removed,
      // or Child removed > Parent removed.
      kAfterParentRemoved,
    };

    enum class LegacyStatus {
      kOk,
      // Child marked `legacy=false` or `legacy=true`, but was never removed
      // (neither directly nor through inheritance from parent).
      kNeverRemoved,
      // Child legacy is kYes but Parent legacy is kNo, and both are removed.
      kWithoutParent,
    };

    Status added = Status::kOk;
    Status deprecated = Status::kOk;
    Status removed = Status::kOk;
    LegacyStatus legacy = LegacyStatus::kOk;

    bool Ok() const {
      return added == Status::kOk && deprecated == Status::kOk && removed == Status::kOk &&
             legacy == LegacyStatus::kOk;
    }
  };

  // Must be called second. Inherits unset fields from `parent`.
  InheritResult Inherit(const Availability& parent);

  // Must be called third. Narrows the availability to the given range, which
  // must be a subset of range().
  void Narrow(VersionRange range);

  // Returns a string representation of the availability for debugging, of the
  // form "<added> <deprecated> <removed> <legacy>", using "_" for null values.
  std::string Debug() const;

 private:
  constexpr explicit Availability(State state) : state_(state) {}

  bool ValidOrder() const;

  State state_ = State::kUnset;
  std::optional<Version> added_, deprecated_, removed_;
  std::optional<Legacy> legacy_;
};

// A version selection is an assignment of versions to platforms.
class VersionSelection final {
 public:
  VersionSelection() = default;

  // Inserts a platform version. Returns true on success, and false if a version
  // was already inserted for this platform.
  bool Insert(Platform platform, Version version);

  // Returns the version for the given platform. Defaults to HEAD if no version
  // was inserted for this platform.
  Version Lookup(const Platform& platform) const;

  // Returns the set of platforms that versions were selected for.
  std::set<Platform, Platform::Compare> Platforms() const;

 private:
  std::map<Platform, Version, Platform::Compare> map_;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_VERSIONING_TYPES_H_
