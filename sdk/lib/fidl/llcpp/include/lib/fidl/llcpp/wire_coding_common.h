// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_WIRE_CODING_COMMON_H_
#define LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_WIRE_CODING_COMMON_H_

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/coding_errors.h>

#include <cstddef>
#include <cstdint>
#include <limits>

#define FIDL_ALIGN_64(a) (((a) + 7ull) & ~7ull)

namespace fidl::internal {

static constexpr uint8_t kWireRecursionDepthInitial = 0;
static constexpr uint8_t kWireRecursionDepthMax = FIDL_RECURSION_DEPTH;

// Represents position in the buffer being encoded or decoded.
class WirePosition {
 public:
  WirePosition() = default;
  WirePosition(const WirePosition&) = default;
  WirePosition(WirePosition&&) = default;
  WirePosition& operator=(const WirePosition&) = default;
  WirePosition& operator=(WirePosition&&) = default;
  constexpr explicit WirePosition(uint8_t* ptr) noexcept : ptr_(ptr) {}

  constexpr uint8_t* get() const noexcept { return ptr_; }
  template <typename T>
  constexpr T* As() const noexcept {
    return reinterpret_cast<T*>(ptr_);
  }

  constexpr WirePosition operator+(size_t size) const noexcept { return WirePosition(ptr_ + size); }
  constexpr WirePosition operator-(size_t size) const noexcept { return WirePosition(ptr_ - size); }

 private:
  uint8_t* ptr_;
};

class WireEncoder;
class WireDecoder;

// Recursion depth calculator and checker.
//
// The false template instantiation performs no checks.
// Skipping these checks has a significant code size impact.
//
// TODO(fxbug.dev/100403) Explore further recursion depth optimizations.
template <bool IsRecursive>
class RecursionDepth {
 public:
  RecursionDepth(const RecursionDepth&) = default;
  RecursionDepth(RecursionDepth&&) noexcept = default;
  RecursionDepth& operator=(const RecursionDepth&) = default;
  RecursionDepth& operator=(RecursionDepth&&) noexcept = default;

  static RecursionDepth Initial() { return RecursionDepth(); }

  template <typename Coder>
  RecursionDepth Add(Coder* coder, size_t diff) {
    return RecursionDepth();
  }

  bool IsValid() const { return true; }

 private:
  RecursionDepth() = default;
};

// Recursion depth checker that performs recursion depth checks.
template <>
class RecursionDepth<true> {
  static constexpr size_t kInvalidDepth = std::numeric_limits<size_t>::max();

 public:
  RecursionDepth(const RecursionDepth&) = default;
  RecursionDepth(RecursionDepth&&) = default;
  RecursionDepth& operator=(const RecursionDepth&) = default;
  RecursionDepth& operator=(RecursionDepth&&) = default;

  static RecursionDepth Initial() { return RecursionDepth(kWireRecursionDepthInitial); }

  template <typename Coder>
  RecursionDepth Add(Coder* coder, size_t diff) {
    if (depth_ + diff > kWireRecursionDepthMax) {
      coder->SetError(kCodingErrorRecursionDepthExceeded);
      return RecursionDepth(kInvalidDepth);
    }
    return RecursionDepth(depth_ + diff);
  }

  bool IsValid() const { return depth_ != kInvalidDepth; }

 private:
  explicit RecursionDepth(size_t depth) noexcept : depth_(depth) {}
  size_t depth_ = kWireRecursionDepthInitial;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_WIRE_CODING_COMMON_H_
