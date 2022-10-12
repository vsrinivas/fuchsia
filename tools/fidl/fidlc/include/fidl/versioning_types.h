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
//               { numeric versions }                        HEAD
//        o------o------o--- ... ---o------o--- ... ---o------o
//        0      1      2        2^63-1   2^63     2^64-2   2^64-1
//
// Internally, this class uses a different format to represent -inf and +inf:
//
//      -inf     { numeric versions }                HEAD   +inf
//        o------o------o--- ... ---o------o--- ... ---o------o
//        0      1      2        2^63-1   2^63     2^64-2   2^64-1
//
// Note that HEAD is bumped down in order to make comparisons work properly.
class Version final {
 public:
  // Succeeds if `ordinal` corresponds to a finite version.
  static std::optional<Version> From(uint64_t ordinal);
  // Succeeds if `str` can be parsed as a numeric version, or is "HEAD".
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
  static constexpr Version Head() { return Version(std::numeric_limits<uint64_t>::max() - 1); }

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
  std::pair<Version, Version> pair() const { return pair_; }

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

// An availability represents the versions when a FIDL element was added (A),
// deprecated (D), and removed (R) from a platform. These versions break the
// platform's timeline into the following regions:
//
//     Present        -- [A, R)
//         Available  -- [A, D), or [A, R) when D is unset
//         Deprecated -- [D, R)
//     Absent         -- (-inf, A) and [R, +inf)
//
// Here is what the timeline looks like for finite versions A, D, and R:
//
//   -inf         A-1  A              D-1  D              R-1  R         +inf
//     o--- ... ---o---o--- ....... ---o---o--- ....... ---o---o--- ... ---o
//     |           |   |-- Available --|   |-- Deprecated -|   |           |
//     |-- Absent -|   |-------------- Present ------------|   |-- Absent -|
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
    return unbounded;
  }

  // An availability advances through four states:
  enum class State {
    // 1. Default constructed. All fields are null.
    kUnset,
    // 2. `Init` succeeded. Some fields might be set, and they are in order.
    kInitialized,
    // 3. `Inherit` succeeded. Now `added` and `removed` are always set.
    kInherited,
    // 4. `Narrow` succeeded. Now `deprecated`, if set, is equal to `added`.
    kNarrowed,
    // One of the steps failed.
    kFailed,
  };

  State state() const { return state_; }

  // Returns the points demarcating the availability: `added`, `removed`, and
  // `deprecated` (if deprecated). Must be in the kInherited or kNarrowed state.
  std::set<Version> points() const;

  // Returns the [added, removed) range. Must be in the kInherit state or later.
  VersionRange range() const;

  // Returns true if the whole range is deprecated, and false if none of it is.
  // Must be in the kNarrowed state (where deprecation is all-or-nothing).
  bool is_deprecated() const;

  // Explicitly mark the availability as failed. Must not have called Init yet.
  void Fail();

  // Must be called first. Initializes the availability from @available fields.
  // Returns false if they do not satisfy `added <= deprecated < removed`. If
  // `deprecated` is set, it must be finite.
  bool Init(std::optional<Version> added, std::optional<Version> deprecated,
            std::optional<Version> removed);

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

    Status added = Status::kOk;
    Status deprecated = Status::kOk;
    Status removed = Status::kOk;

    bool Ok() const {
      return added == Status::kOk && deprecated == Status::kOk && removed == Status::kOk;
    }
  };

  // Must be called second. Inherits unset fields from `parent`.
  InheritResult Inherit(const Availability& parent);

  // Must be called third. Narrows the availability to the given range, which
  // must be a subset of range().
  void Narrow(VersionRange range);

  // Returns a string representation of the availability for debugging use, of
  // the form "<added> <deprecated> <removed>", using "_" for null values.
  std::string Debug() const;

 private:
  constexpr explicit Availability(State state) : state_(state) {}

  bool ValidOrder() const;

  State state_ = State::kUnset;
  std::optional<Version> added_, deprecated_, removed_;
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
