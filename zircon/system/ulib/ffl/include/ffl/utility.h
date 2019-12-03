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

// Traits type the determines the precision of the given integer type.
template <typename T>
struct IntegerPrecisionType {
  static_assert(sizeof(T) != sizeof(T), "T must be a standard integer type!");
};

// The precision of signed values does not include the signed bit.
template <>
struct IntegerPrecisionType<int8_t> {
  static constexpr size_t value = 7;
};
template <>
struct IntegerPrecisionType<int16_t> {
  static constexpr size_t value = 15;
};
template <>
struct IntegerPrecisionType<int32_t> {
  static constexpr size_t value = 31;
};
template <>
struct IntegerPrecisionType<int64_t> {
  static constexpr size_t value = 63;
};

// The precision of unsigned values covers the full range.
template <>
struct IntegerPrecisionType<uint8_t> {
  static constexpr size_t value = 8;
};
template <>
struct IntegerPrecisionType<uint16_t> {
  static constexpr size_t value = 16;
};
template <>
struct IntegerPrecisionType<uint32_t> {
  static constexpr size_t value = 32;
};
template <>
struct IntegerPrecisionType<uint64_t> {
  static constexpr size_t value = 64;
};

template <typename T>
static constexpr size_t IntegerPrecision = IntegerPrecisionType<T>::value;

// Trait type to determine the best-fitting integer for a given sign and
// precision in bits.
template <bool Signed, size_t Precision, typename = void>
struct BestFittingType;

// Signed values require space for the signed bit, precision covers the positive
// range.
template <size_t Precision>
struct BestFittingType<true, Precision, std::enable_if_t<(Precision < 8)>> {
  using Type = int8_t;
};
template <size_t Precision>
struct BestFittingType<true, Precision, std::enable_if_t<(Precision >= 8 && Precision < 16)>> {
  using Type = int16_t;
};
template <size_t Precision>
struct BestFittingType<true, Precision, std::enable_if_t<(Precision >= 16 && Precision < 32)>> {
  using Type = int32_t;
};
template <size_t Precision>
struct BestFittingType<true, Precision, std::enable_if_t<(Precision >= 32)>> {
  using Type = int64_t;
};

// Unsigned values do not have a signed bit, precision covers the entire range.
template <size_t Precision>
struct BestFittingType<false, Precision, std::enable_if_t<(Precision <= 8)>> {
  using Type = uint8_t;
};
template <size_t Precision>
struct BestFittingType<false, Precision, std::enable_if_t<(Precision > 8 && Precision <= 16)>> {
  using Type = uint16_t;
};
template <size_t Precision>
struct BestFittingType<false, Precision, std::enable_if_t<(Precision > 16 && Precision <= 32)>> {
  using Type = uint32_t;
};
template <size_t Precision>
struct BestFittingType<false, Precision, std::enable_if_t<(Precision > 32)>> {
  using Type = uint64_t;
};

template <bool Signed, size_t Precision>
using BestFitting = typename BestFittingType<Signed, Precision>::Type;

// Changes the signedness of Integer to match the signedness of Reference,
// preserving the original size of Integer.
template <typename Reference, typename Integer>
using MatchSignedOrUnsigned =
    std::conditional_t<std::is_signed_v<Reference>, std::make_signed_t<Integer>,
                       std::make_unsigned_t<Integer>>;

// Clamps the given Integer value to the range of Result. Optimized for all
// combinations of sizes and signedness.
template <typename Result, typename Integer,
          std::enable_if_t<std::is_integral_v<Result> && std::is_integral_v<Integer> &&
                               std::is_unsigned_v<Result> && std::is_signed_v<Integer>,
                           int> = 0>
constexpr Result ClampCast(Integer value) {
  if (value <= 0) {
    return 0;
  }
  if constexpr (sizeof(Result) < sizeof(Integer)) {
    constexpr auto kMax = std::numeric_limits<Result>::max();
    if (value > static_cast<Integer>(kMax)) {
      return kMax;
    }
  }
  return static_cast<Result>(value);
}
template <typename Result, typename Integer,
          std::enable_if_t<std::is_integral_v<Result> && std::is_integral_v<Integer> &&
                               std::is_unsigned_v<Result> && std::is_unsigned_v<Integer>,
                           int> = 1>
constexpr Result ClampCast(Integer value) {
  if constexpr (sizeof(Result) < sizeof(Integer)) {
    constexpr auto kMax = std::numeric_limits<Result>::max();
    if (value > kMax) {
      return kMax;
    }
  }
  return static_cast<Result>(value);
}
template <typename Result, typename Integer,
          std::enable_if_t<std::is_integral_v<Result> && std::is_integral_v<Integer> &&
                               std::is_signed_v<Result> && std::is_unsigned_v<Integer>,
                           int> = 2>
constexpr Result ClampCast(Integer value) {
  if constexpr (sizeof(Result) <= sizeof(Integer)) {
    constexpr auto kMax = std::numeric_limits<Result>::max();
    if (value > static_cast<Integer>(kMax)) {
      return kMax;
    }
  }
  return static_cast<Result>(value);
}
template <typename Result, typename Integer,
          std::enable_if_t<std::is_integral_v<Result> && std::is_integral_v<Integer> &&
                               std::is_signed_v<Result> && std::is_signed_v<Integer>,
                           int> = 3>
constexpr Result ClampCast(Integer value) {
  if constexpr (sizeof(Result) < sizeof(Integer)) {
    constexpr auto kMin = std::numeric_limits<Result>::min();
    constexpr auto kMax = std::numeric_limits<Result>::max();
    if (value < kMin) {
      return kMin;
    } else if (value > kMax) {
      return kMax;
    }
  }
  return static_cast<Result>(value);
}

}  // namespace ffl

#endif  // FFL_UTILITY_H_
