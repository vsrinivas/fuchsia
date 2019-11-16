// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef FFL_UTILITY_H_
#define FFL_UTILITY_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

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

// Changes the signedness of Integer to match the signedness of Reference,
// preserving the original size of Integer.
template <typename Reference, typename Integer>
using MatchSignedOrUnsigned =
    std::conditional_t<std::is_signed_v<Reference>, std::make_signed_t<Integer>,
                       std::make_unsigned_t<Integer>>;

// Clamps the given Integer value to the range of Result. Optimized for all
// combinations of sizes and signedness.
template <typename Result, typename Integer,
          typename = std::enable_if_t<std::is_integral_v<Result> && std::is_integral_v<Integer>>>
constexpr Result ClampCast(Integer value) {
  constexpr auto kMin = std::numeric_limits<Result>::min();
  constexpr auto kMax = std::numeric_limits<Result>::max();

  if constexpr (std::is_unsigned_v<Result> && std::is_signed_v<Integer>) {
    if (value <= 0) {
      return 0;
    }
    if constexpr (sizeof(Result) < sizeof(Integer)) {
      if (value > static_cast<Integer>(kMax)) {
        return kMax;
      }
    }
    return static_cast<Result>(value);
  } else if (std::is_unsigned_v<Result> && std::is_unsigned_v<Integer>) {
    if constexpr (sizeof(Result) < sizeof(Integer)) {
      if (value > kMax) {
        return kMax;
      }
    }
    return static_cast<Result>(value);
  } else if (std::is_signed_v<Result> && std::is_unsigned_v<Integer>) {
    if constexpr (sizeof(Result) <= sizeof(Integer)) {
      if (value > static_cast<Integer>(kMax)) {
        return kMax;
      }
    }
    return static_cast<Result>(value);
  } else if (std::is_signed_v<Result> && std::is_signed_v<Integer>) {
    if constexpr (sizeof(Result) < sizeof(Integer)) {
      if (value < kMin) {
        return kMin;
      } else if (value > kMax) {
        return kMax;
      }
    }
    return static_cast<Result>(value);
  }

  // Silence warnings when compiling with GCC.
  static_cast<void>(kMin);
  static_cast<void>(kMax);
}

}  // namespace ffl

#endif  // FFL_UTILITY_H_
