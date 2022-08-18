// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BITFIELD_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BITFIELD_H_

#include <stdint.h>
#include <stdio.h>
#include <zircon/compiler.h>

#include <array>
#include <type_traits>

namespace wlan {
namespace common {

template <typename ValueType>
class BitField {
 public:
  static_assert(std::is_unsigned<ValueType>::value, "BitField only supports unsigned value types.");

  constexpr explicit BitField(ValueType val) : val_(val) {}
  constexpr BitField() = default;

  constexpr size_t len() { return sizeof(ValueType); }
  void clear() { val_ = 0; }
  void set_val(ValueType val) { val_ = val; }
  ValueType* mut_val() { return &val_; }

  ValueType val() const { return val_; }

  template <unsigned int offset, size_t len>
  ValueType get_bits() const {
    return (val_ & mask<offset, len>()) >> offset;
  }

  template <unsigned int offset, size_t len>
  void set_bits(ValueType value) {
    ValueType cleared = val_ & ~mask<offset, len>();
    val_ = cleared | ((value << offset) & mask<offset, len>());
  }

 private:
  template <unsigned int offset, size_t len>
  constexpr static ValueType mask() {
    static_assert(len > 0, "BitField member length must be positive");
    static_assert(offset < (sizeof(ValueType) * 8),
                  "offset must be less than size of the BitField");
    static_assert(offset + len <= (sizeof(ValueType) * 8),
                  "offset + len must be less than or equal to size of the BitField");
    return ((1ull << len) - 1) << offset;
  }

  ValueType val_ = 0;
} __PACKED;

/// A bitfield of arbitrary length, mapped onto an underlying byte array.
/// This bitfield conforms to the definition in IEEE Std 802.11-2016, 9.2.2.
/// Specifically, the least significant (zero) bit is defined to be the zero
/// bit of the first byte, i.e. a little endian ordering. This means that the
/// bit offsets defined in IEEE may be mapped directly into WLAN_BIT_FIELD
/// definitions.
///
/// Example bit ordering for N=2:
/// [0b00000000, 0b00000000]
///    ^^^^^^^^    ^^^^^^^^
///    76543210    fedcba98
///
template <size_t N>
class LittleEndianBitField {
 public:
  constexpr explicit LittleEndianBitField(std::array<uint8_t, N> val) : val_(val) {}
  constexpr LittleEndianBitField() = default;

  constexpr size_t len() { return N; }
  void clear() { val_ = {}; }
  void set_val(std::array<uint8_t, N> val) { val_ = val; }
  std::array<uint8_t, N>* mut_val() { return &val_; }

  std::array<uint8_t, N> val() const { return val_; }

  template <unsigned int first_bit_idx, size_t len>
  uint64_t get_bits() const {
    perform_static_asserts<first_bit_idx, len>();
    uint64_t value = 0;
    size_t range = last_cell<first_bit_idx, len>() - first_cell<first_bit_idx, len>();
    // We generate a negative range using a subtracted offset to avoid overflow.
    for (size_t i = 0; i <= range; i++) {
      size_t idx = last_cell<first_bit_idx, len>() - i;
      value = (value << 8) | val_[idx];
    }
    return (value >> (first_bit_idx % 8)) & mask<0, len>();
  }

  template <unsigned int first_bit_idx, size_t len>
  void set_bits(uint64_t value) {
    perform_static_asserts<first_bit_idx, len>();
    uint64_t offset_value = value << (first_bit_idx % 8);
    uint64_t clear_mask = ~mask<first_bit_idx % 8, len>();
    uint64_t cell_mask = mask<0, 8>();
    // We generate a negative range using a subtracted offset to avoid overflow.
    for (size_t idx = first_cell<first_bit_idx, len>(); idx <= last_cell<first_bit_idx, len>();
         idx++) {
      val_[idx] &= static_cast<uint8_t>(clear_mask & cell_mask);
      val_[idx] |= static_cast<uint8_t>(offset_value & cell_mask);
      offset_value >>= 8;
      clear_mask >>= 8;
    }
  }

 private:
  template <unsigned int first_bit_idx, size_t len>
  constexpr static void perform_static_asserts() {
    static_assert(len > 0, "LittleEndianBitField member length must be positive");
    static_assert(first_bit_idx % 8 + len <= 64,
                  "LittleEndianBitField member cannot overlap more than 8 bytes");
    static_assert(first_bit_idx + len <= N * 8,
                  "LittleEndianBitField member cannot overflow array");
  }

  template <unsigned int first_bit_idx_in_cell, size_t len>
  constexpr static uint64_t mask() {
    if (len + first_bit_idx_in_cell == 64) {
      return ~0ull << first_bit_idx_in_cell;
    }
    return ((1ull << len) - 1) << first_bit_idx_in_cell;
  }

  template <unsigned int first_bit_idx, size_t len>
  constexpr static size_t first_cell() {
    return first_bit_idx / 8;
  }

  template <unsigned int first_bit_idx, size_t len>
  constexpr static size_t last_cell() {
    return (len + first_bit_idx - 1) / 8;
  }

  std::array<uint8_t, N> val_;
} __PACKED;

// Specialize the mask function for full 64-bit fields, since (1 << 64) - 1 is
// an error.
template <>
template <>
constexpr uint64_t BitField<uint64_t>::mask<0, 64>() {
  return ~0ull;
}

template <typename AddrType, typename ValueType, AddrType A>
class AddressableBitField : public BitField<ValueType> {
 public:
  static constexpr AddrType addr() { return A; }

  constexpr explicit AddressableBitField(ValueType val) : BitField<ValueType>(val) {}
  constexpr AddressableBitField() = default;
};

namespace internal {
// Due to the way printf format specifiers work, we don't want to require all
// bitfield getters and setters to use uint64_t. The following type traits allow
// selecting the smallest uintN_t type that will hold the length of the
// bitfield.
constexpr size_t next_bitsize(size_t n) {
  // clang-format off
    return n < 8 ? 8 :
           n < 16 ? 16 :
           n < 32 ? 32 : 64;
  // clang-format on
}

template <size_t N>
struct IntegerType;

template <>
struct IntegerType<8> {
  using type = uint8_t;
};
template <>
struct IntegerType<16> {
  using type = uint16_t;
};
template <>
struct IntegerType<32> {
  using type = uint32_t;
};
template <>
struct IntegerType<64> {
  using type = uint64_t;
};

template <size_t N>
struct Integer : IntegerType<next_bitsize(N)> {};
}  // namespace internal

#define WLAN_BIT_FIELD(name, offset, len)                               \
  void set_##name(::wlan::common::internal::Integer<len>::type val) {   \
    this->template set_bits<offset, len>(val);                          \
  }                                                                     \
  constexpr ::wlan::common::internal::Integer<len>::type name() const { \
    return static_cast<::wlan::common::internal::Integer<len>::type>(   \
        this->template get_bits<offset, len>());                        \
  }

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BITFIELD_H_
