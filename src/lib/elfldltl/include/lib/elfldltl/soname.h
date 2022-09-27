// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_SONAME_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_SONAME_H_

#include <cassert>
#include <cstdint>
#include <string_view>

#include "gnu-hash.h"

namespace elfldltl {

// This provides an optimized type for holding a DT_SONAME / DT_NEEDED string.
// It always hashes the string to make equality comparisons faster.
class Soname {
 public:
  constexpr Soname() = default;

  constexpr Soname(const Soname&) = default;

  constexpr explicit Soname(std::string_view name)
      : name_(name.data()), size_(static_cast<uint32_t>(name.size())), hash_(GnuHashString(name)) {
    assert(name.size() == size_);
  }

  constexpr Soname& operator=(const Soname&) noexcept = default;

  constexpr std::string_view str() const { return {name_, size_}; }

  constexpr uint32_t hash() const { return hash_; }

  constexpr bool operator==(const Soname& other) const {
    return other.hash_ == hash_ && other.str() == str();
  }

  constexpr bool operator!=(const Soname& other) const {
    return other.hash_ != hash_ || other.str() != str();
  }

  constexpr bool operator<(const Soname& other) const { return str() < other.str(); }

  constexpr bool operator<=(const Soname& other) const { return str() <= other.str(); }

  constexpr bool operator>(const Soname& other) const { return str() > other.str(); }

  constexpr bool operator>=(const Soname& other) const { return str() >= other.str(); }

 private:
  // This stores a pointer and 32-bit length directly rather than just using
  // std::string_view so that the whole object is still only two 64-bit words.
  // Crucially, both x86-64 and AArch64 ABIs pass and return trivial two-word
  // objects in registers but anything larger in memory, so this keeps passing
  // Soname as cheap as passing std::string_view.  This limits lengths to 4GiB,
  // which is far more than the practical limit.
  const char* name_ = nullptr;
  uint32_t size_ = 0;
  uint32_t hash_ = 0;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_SONAME_H_
