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

VersionRange Availability::range() const {
  ZX_ASSERT(state_ == State::kInherited || state_ == State::kNarrowed);
  return VersionRange(added_.value(), removed_.value());
}

std::set<Version> Availability::points() const {
  ZX_ASSERT(state_ == State::kInherited || state_ == State::kNarrowed);
  std::set<Version> result{added_.value(), removed_.value()};
  if (deprecated_) {
    result.insert(deprecated_.value());
  }
  return result;
}

bool Availability::is_deprecated() const {
  ZX_ASSERT(state_ == State::kNarrowed);
  return deprecated_.has_value();
}

void Availability::Fail() {
  ZX_ASSERT_MSG(state_ == State::kUnset, "called Fail in the wrong order");
  state_ = State::kFailed;
}

bool Availability::Init(std::optional<Version> added, std::optional<Version> deprecated,
                        std::optional<Version> removed) {
  ZX_ASSERT_MSG(state_ == State::kUnset, "called Init in the wrong order");
  ZX_ASSERT_MSG(deprecated != Version::NegInf(), "deprecated version must be finite, got -inf");
  ZX_ASSERT_MSG(deprecated != Version::PosInf(), "deprecated version must be finite, got +inf");
  added_ = added;
  deprecated_ = deprecated;
  removed_ = removed;
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

  if (result.Ok()) {
    ZX_ASSERT(added_ && removed_);
    ZX_ASSERT(ValidOrder());
    state_ = State::kInherited;
  } else {
    state_ = State::kFailed;
  }
  return result;
}

void Availability::Narrow(VersionRange range) {
  ZX_ASSERT_MSG(state_ == State::kInherited, "called Narrow in the wrong order");
  state_ = State::kNarrowed;
  auto [a, b] = range.pair();
  ZX_ASSERT_MSG(a >= added_ && b <= removed_, "must narrow to a subrange");
  added_ = a;
  removed_ = b;
  if (deprecated_ && a >= deprecated_.value()) {
    deprecated_ = a;
  } else {
    deprecated_ = std::nullopt;
  }
}

std::string Availability::Debug() const {
  std::stringstream ss;
  auto str = [&](const std::optional<Version>& version) {
    return version ? version->ToString() : "_";
  };
  ss << str(added_) << " " << str(deprecated_) << " " << str(removed_);
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
