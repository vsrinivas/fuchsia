// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/versioning_types.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/utils.h"

namespace fidl {

std::optional<Platform> Platform::Parse(std::string str) {
  if (utils::IsValidLibraryComponent(str)) {
    return Platform(std::move(str));
  }
  return std::nullopt;
}

std::optional<Version> Version::From(uint64_t ordinal) {
  if (ordinal == Head().ordinal()) {
    return Head();
  }
  if (ordinal == Legacy().ordinal()) {
    return Legacy();
  }
  if (ordinal == 0 || ordinal >= (1ul << 63)) {
    return std::nullopt;
  }
  return Version(ordinal);
}

std::optional<Version> Version::Parse(std::string_view str) {
  // We need this check because ParseNumeric returns 0 for an empty string.
  if (str.empty()) {
    return std::nullopt;
  }
  if (str == "HEAD") {
    return Head();
  }
  if (str == "LEGACY") {
    return Legacy();
  }
  uint64_t value;
  if (utils::ParseNumeric(str, &value) != utils::ParseNumericResult::kSuccess) {
    return std::nullopt;
  }
  return From(value);
}

uint64_t Version::ordinal() const {
  switch (value_) {
    case NegInf().value_:
    case PosInf().value_:
      ZX_PANIC("infinite versions do not have an ordinal");
    case Head().value_:
      return std::numeric_limits<uint64_t>::max() - 1;
    case Legacy().value_:
      return std::numeric_limits<uint64_t>::max();
    default:
      return value_;
  }
}

std::string Version::ToString() const {
  switch (value_) {
    case Version::NegInf().value_:
      return "-inf";
    case Version::PosInf().value_:
      return "+inf";
    case Version::Head().value_:
      return "HEAD";
    case Version::Legacy().value_:
      return "LEGACY";
    default:
      return std::to_string(value_);
  }
}

bool VersionRange::Contains(Version version) const {
  auto [a, b] = pair_;
  return a <= version && version < b;
}

// static
std::optional<VersionRange> VersionRange::Intersect(const std::optional<VersionRange>& lhs,
                                                    const std::optional<VersionRange>& rhs) {
  if (!lhs || !rhs) {
    return std::nullopt;
  }
  auto [a1, b1] = lhs.value().pair_;
  auto [a2, b2] = rhs.value().pair_;
  if (b1 <= a2 || b2 <= a1) {
    return std::nullopt;
  }
  return VersionRange(std::max(a1, a2), std::min(b1, b2));
}

bool VersionSet::Contains(Version version) const {
  auto& [x, maybe_y] = ranges_;
  return x.Contains(version) || (maybe_y && maybe_y.value().Contains(version));
}

// static
std::optional<VersionSet> VersionSet::Intersect(const std::optional<VersionSet>& lhs,
                                                const std::optional<VersionSet>& rhs) {
  if (!lhs || !rhs) {
    return std::nullopt;
  }
  auto& [x1, x2] = lhs.value().ranges_;
  auto& [y1, y2] = rhs.value().ranges_;
  std::optional<VersionRange> z1, z2;
  for (auto range : {
           VersionRange::Intersect(x1, y1),
           VersionRange::Intersect(x1, y2),
           VersionRange::Intersect(x2, y1),
           VersionRange::Intersect(x2, y2),
       }) {
    if (!range) {
      continue;
    }
    if (!z1) {
      z1 = range;
    } else if (!z2) {
      z2 = range;
    } else {
      ZX_PANIC("set intersection is more than two pieces");
    }
  }
  if (!z1) {
    ZX_ASSERT(!z2);
    return std::nullopt;
  }
  return VersionSet(z1.value(), z2);
}

VersionSet Availability::set() const {
  ZX_ASSERT(state_ == State::kInherited || state_ == State::kNarrowed);
  VersionRange range(added_.value(), removed_.value());
  switch (legacy_.value()) {
    case Legacy::kNotApplicable:
    case Legacy::kNo:
      return VersionSet(range);
    case Legacy::kYes:
      return VersionSet(range, VersionRange(Version::Legacy(), Version::PosInf()));
  }
}

std::set<Version> Availability::points() const {
  ZX_ASSERT(state_ == State::kInherited || state_ == State::kNarrowed);
  std::set<Version> result{added_.value(), removed_.value()};
  if (deprecated_) {
    result.insert(deprecated_.value());
  }
  if (legacy_.value() == Legacy::kYes) {
    ZX_ASSERT(result.insert(Version::Legacy()).second);
    ZX_ASSERT(result.insert(Version::PosInf()).second);
  }
  return result;
}

VersionRange Availability::range() const {
  ZX_ASSERT(state_ == State::kNarrowed);
  return VersionRange(added_.value(), removed_.value());
}

bool Availability::is_deprecated() const {
  ZX_ASSERT(state_ == State::kNarrowed);
  return deprecated_.has_value();
}

void Availability::Fail() {
  ZX_ASSERT_MSG(state_ == State::kUnset, "called Fail in the wrong order");
  state_ = State::kFailed;
}

bool Availability::Init(InitArgs args) {
  ZX_ASSERT_MSG(state_ == State::kUnset, "called Init in the wrong order");
  ZX_ASSERT_MSG(args.added != Version::Legacy(), "adding at LEGACY is not allowed");
  ZX_ASSERT_MSG(args.removed != Version::Legacy(), "removing at LEGACY is not allowed");
  ZX_ASSERT_MSG(args.deprecated != Version::Legacy(), "deprecating at LEGACY is not allowed");
  ZX_ASSERT_MSG(args.deprecated != Version::NegInf(),
                "deprecated version must be finite, got -inf");
  ZX_ASSERT_MSG(args.deprecated != Version::PosInf(),
                "deprecated version must be finite, got +inf");
  ZX_ASSERT_MSG(args.legacy != Legacy::kNotApplicable, "legacy cannot be kNotApplicable");
  added_ = args.added;
  deprecated_ = args.deprecated;
  removed_ = args.removed;
  legacy_ = args.legacy;
  bool valid = ValidOrder();
  state_ = valid ? State::kInitialized : State::kFailed;
  return valid;
}

bool Availability::ValidOrder() const {
  auto a = added_.value_or(Version::NegInf());
  auto d = deprecated_.value_or(a);
  auto r = removed_.value_or(Version::PosInf());
  return a <= d && d < r;
}

Availability::InheritResult Availability::Inherit(const Availability& parent) {
  ZX_ASSERT_MSG(state_ == State::kInitialized, "called Inherit in the wrong order");
  ZX_ASSERT_MSG(parent.state_ == State::kInherited, "must call Inherit on parent first");
  InheritResult result;
  // Inherit and validate `added`.
  if (!added_) {
    added_ = parent.added_.value();
  } else if (added_.value() < parent.added_.value()) {
    result.added = InheritResult::Status::kBeforeParentAdded;
  } else if (added_.value() >= parent.removed_.value()) {
    result.added = InheritResult::Status::kAfterParentRemoved;
  }
  // Inherit and validate `removed`.
  if (!removed_) {
    removed_ = parent.removed_.value();
  } else if (removed_.value() <= parent.added_.value()) {
    result.removed = InheritResult::Status::kBeforeParentAdded;
  } else if (removed_.value() > parent.removed_.value()) {
    result.removed = InheritResult::Status::kAfterParentRemoved;
  }
  // Inherit and validate `deprecated`.
  if (!deprecated_) {
    // Only inherit deprecation if it occurs before this element is removed.
    if (parent.deprecated_ && parent.deprecated_.value() < removed_.value()) {
      // As a result of inheritance, we can end up with deprecated < added:
      //
      //     @available(added=1, deprecated=5, removed=10)
      //     type Foo = struct {
      //         @available(added=7)
      //         bar bool;
      //     };
      //
      // To maintain `added <= deprecated < removed` in this case, we use
      // std::max below. A different choice would be to disallow this, and
      // consider `Foo` frozen once deprecated. However, going down this path
      // leads to contradictions with the overall design of FIDL Versioning.
      deprecated_ = std::max(parent.deprecated_.value(), added_.value());
    }
  } else if (deprecated_.value() < parent.added_.value()) {
    result.deprecated = InheritResult::Status::kBeforeParentAdded;
  } else if (deprecated_.value() >= parent.removed_.value()) {
    result.deprecated = InheritResult::Status::kAfterParentRemoved;
  } else if (parent.deprecated_ && deprecated_.value() > parent.deprecated_.value()) {
    result.deprecated = InheritResult::Status::kAfterParentDeprecated;
  }
  // Inherit and validate `legacy`.
  if (!legacy_) {
    if (removed_.value() == parent.removed_.value()) {
      // Only inherit if the parent was removed at the same time. For example:
      //
      //     @available(added=1, removed=100, legacy=true)
      //     type Foo = table {
      //         @available(removed=2) 1: string bar;
      //         @available(added=2)   1: string bar:10;
      //         @available(removed=3) 2: bool qux;
      //     };
      //
      // It's crucial we do not inherit legacy=true on the first `bar`,
      // otherwise there will be two `bar` fields that collide at LEGACY. We
      // also don't want to inherit legacy=true for `qux`: it had no legacy
      // legacy support when it was removed at 3, so it doesn't make sense to
      // change that when we later remove the entire table at 100.
      //
      // An alternative is to inherit when the child has no explicit `removed`.
      // We prefer to base it on post-inheritance equality so that adding or
      // removing a redundant `removed=...` on the child is purely stylistic.
      legacy_ = parent.legacy_.value();
    } else {
      ZX_ASSERT_MSG(
          removed_.value() != Version::PosInf(),
          "impossible for child to be removed at +inf if parent is not also removed at +inf");
      // By default, removed elements are not added back at LEGACY.
      legacy_ = Legacy::kNo;
    }
  } else if (removed_.value() == Version::PosInf()) {
    // Legacy is not applicable if the element is never removed. Note that we
    // cannot check this earlier (e.g. in Init) because we don't know if the
    // element is removed or not until performing inheritance.
    result.legacy = InheritResult::LegacyStatus::kNeverRemoved;
  } else if (legacy_.value() == Legacy::kYes && parent.legacy_.value() == Legacy::kNo) {
    // We can't re-add the child at LEGACY without its parent.
    result.legacy = InheritResult::LegacyStatus::kWithoutParent;
  }

  if (result.Ok()) {
    ZX_ASSERT(added_ && removed_ && legacy_);
    ZX_ASSERT(ValidOrder());
    state_ = State::kInherited;
  } else {
    state_ = State::kFailed;
  }
  return result;
}

void Availability::Narrow(VersionRange range) {
  ZX_ASSERT_MSG(state_ == State::kInherited, "called Narrow in the wrong order");
  auto [a, b] = range.pair();
  if (a == Version::Legacy()) {
    ZX_ASSERT_MSG(b == Version::PosInf(), "legacy range must be [LEGACY, +inf)");
    ZX_ASSERT_MSG(legacy_.value() != Legacy::kNo, "must be present at LEGACY");
  } else {
    ZX_ASSERT_MSG(a >= added_ && b <= removed_, "must narrow to a subrange");
  }
  added_ = a;
  removed_ = b;
  if (deprecated_ && a >= deprecated_.value()) {
    deprecated_ = a;
  } else {
    deprecated_ = std::nullopt;
  }
  if (a <= Version::Legacy() && b > Version::Legacy()) {
    legacy_ = Legacy::kNotApplicable;
  } else {
    legacy_ = Legacy::kNo;
  }
  state_ = State::kNarrowed;
}

template <typename T>
static std::string ToString(const std::optional<T>& opt) {
  return opt ? ToString(opt.value()) : "_";
}

static std::string ToString(const Version& version) { return version.ToString(); }

static std::string ToString(Availability::Legacy legacy) {
  switch (legacy) {
    case Availability::Legacy::kNotApplicable:
      return "n/a";
    case Availability::Legacy::kNo:
      return "no";
    case Availability::Legacy::kYes:
      return "yes";
  }
}

std::string Availability::Debug() const {
  std::stringstream ss;
  ss << ToString(added_) << " " << ToString(deprecated_) << " " << ToString(removed_) << " "
     << ToString(legacy_);
  return ss.str();
}

bool VersionSelection::Insert(Platform platform, Version version) {
  auto [_, inserted] = map_.emplace(std::move(platform), version);
  return inserted;
}

Version VersionSelection::Lookup(const Platform& platform) const {
  const auto iter = map_.find(platform);
  if (iter == map_.end()) {
    return Version::Head();
  }
  return iter->second;
}

std::set<Platform, Platform::Compare> VersionSelection::Platforms() const {
  std::set<Platform, Platform::Compare> platforms;
  for (auto& [platform, version] : map_) {
    platforms.insert(platform);
  }
  return platforms;
}

}  // namespace fidl
