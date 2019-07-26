// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace ffl {

static_assert(-1 == ~0, "FFL requires a two's complement architecture!");

// Type tag used to disambiguate single-argument template constructors.
struct Init {};

// Type representing a zero-based bit ordinal.
template <size_t Ordinal>
struct Bit {};

// Type representing resolution in terms of fractional bits.
template <size_t FractionalBits>
struct Resolution {};

// Typed constant representing the bit position around which to round.
template <size_t Place>
constexpr auto ToPlace = Bit<Place>{};

// Type trait to determine the size of the intermediate result of integer arithmetic.
template <typename T>
struct IntermediateType;

template <>
struct IntermediateType<uint8_t> {
  using Type = uint16_t;
};
template <>
struct IntermediateType<uint16_t> {
  using Type = uint32_t;
};
template <>
struct IntermediateType<uint32_t> {
  using Type = uint64_t;
};
template <>
struct IntermediateType<uint64_t> {
  using Type = uint64_t;
};
template <>
struct IntermediateType<int8_t> {
  using Type = int16_t;
};
template <>
struct IntermediateType<int16_t> {
  using Type = int32_t;
};
template <>
struct IntermediateType<int32_t> {
  using Type = int64_t;
};
template <>
struct IntermediateType<int64_t> {
  using Type = int64_t;
};

}  // namespace ffl
