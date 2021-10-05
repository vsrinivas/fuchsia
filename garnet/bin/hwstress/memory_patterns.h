// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_MEMORY_PATTERNS_H_
#define GARNET_BIN_HWSTRESS_MEMORY_PATTERNS_H_

#include <endian.h>
#include <lib/stdcompat/span.h>

#include <optional>

#include "src/lib/fxl/strings/string_printf.h"
#include "util.h"

// This file contains functions for generating patterns, writing
// patterns to a span of memory, and verifying patterns from a span of
// memory.

namespace hwstress {

// Rotate the given multi-word pattern right by N bits.
std::vector<uint64_t> RotatePattern(std::vector<uint64_t> v, uint64_t n);

// Invert (bitwise negate) the words in the vector.
std::vector<uint64_t> NegateWords(std::vector<uint64_t> v);

// Return a constant word as a pattern.
//
// The word is always written in memory as a big-endian word. That is, the
// pattern 0x1122334455667788 will be written out as bytes 0x11, 0x22, ..., 0x88
// at increasing memory addresses.
inline auto SimplePattern(uint64_t word) {
  return [w = htobe64(word)]() { return w; };
}

// Invert the given pattern.
template <typename Pattern>
inline auto InvertPattern(Pattern p) {
  return [p = std::move(p)]() mutable { return ~p(); };
}

// Return a pseudo-random stream of values.
inline auto RandomPattern() {
  std::random_device device;
  fast_64bit_rng rng(device());
  return [rng = rng]() mutable { return rng(); };
}

// Repeat the same multi-word pattern.
//
// The values are written to memory in big-endian format. That is, the
// vector [0x1122, 0x3344, 0x5566, 0x7788] will be written out as bytes
// 0x11, 0x22, ..., 0x88 at increasing memory addresses.
inline auto MultiWordPattern(std::vector<uint64_t> pattern) {
  uint64_t i = 0;

  // Convert to big-endian format.
  for (uint64_t& word : pattern) {
    word = htobe64(word);
  }

  return [i, pattern = std::move(pattern)]() mutable { return pattern[i++ % pattern.size()]; };
}

// Write the given pattern out to memory.
//
// Patterns are written out in native-endian format. If a particular
// endian conversion is required, it must be converted by the PatternGenerator.
template <typename PatternGenerator>
void WritePattern(cpp20::span<uint8_t> range, PatternGenerator pattern) {
  auto* __restrict start = reinterpret_cast<uint64_t*>(range.begin());
  size_t words = range.size_bytes() / sizeof(uint64_t);

  for (size_t i = 0; i < words; i++) {
    start[i] = pattern();
  }
}

// Verify the given pattern is in memory.
template <typename PatternGenerator>
std::optional<std::string> VerifyPattern(cpp20::span<uint8_t> range, PatternGenerator pattern) {
  auto* __restrict start = reinterpret_cast<uint64_t*>(range.begin());
  size_t words = range.size_bytes() / sizeof(uint64_t);

  // Find any mismatches.
  for (size_t i = 0; i < words; i++) {
    uint64_t expected = pattern();
    uint64_t actual = start[i];

    if (unlikely(expected != actual)) {
      return fxl::StringPrintf("Expected 0x%016lx, got 0x%16lx at offset %ld.", expected, actual,
                               i * sizeof(uint64_t));
    }
  }

  return std::nullopt;
}

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_MEMORY_PATTERNS_H_
