// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <type_traits>

namespace wlan {
namespace common {

template <typename ValueType> class BitField {
   public:
    static_assert(std::is_unsigned<ValueType>::value,
                  "BitField only supports unsigned value types.");

    constexpr explicit BitField(ValueType val) : val_(val) {}
    constexpr BitField() = default;

    constexpr size_t len() { return sizeof(ValueType); }
    void clear() { val_ = 0; }
    void set_val(ValueType val) { val_ = val; }
    ValueType* mut_val() { return &val_; }

    ValueType val() const { return val_; }

    template <unsigned int offset, size_t len> ValueType get_bits() const {
        return (val_ & mask<offset, len>()) >> offset;
    }

    template <unsigned int offset, size_t len> void set_bits(ValueType value) {
        ValueType cleared = val_ & ~mask<offset, len>();
        val_ = cleared | ((value << offset) & mask<offset, len>());
    }

   private:
    template <unsigned int offset, size_t len> constexpr static ValueType mask() {
        static_assert(len > 0, "BitField member length must be positive");
        static_assert(offset < (sizeof(ValueType) * 8),
                      "offset must be less than size of the BitField");
        static_assert(offset + len <= (sizeof(ValueType) * 8),
                      "offset + len must be less than or equal to size of the BitField");
        return ((1ull << len) - 1) << offset;
    }

    ValueType val_ = 0;
};

// Specialize the mask function for full 64-bit fields, since (1 << 64) - 1 is an error.
template <> template <> constexpr uint64_t BitField<uint64_t>::mask<0, 64>() {
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
// Due to the way printf format specifiers work, we don't want to require all bitfield getters and
// setters to use uint64_t. The following type traits allow selecting the smallest uintN_t type that
// will hold the length of the bitfield.
constexpr size_t next_bitsize(size_t n) {
    // clang-format off
    return n < 8 ? 8 :
           n < 16 ? 16 :
           n < 32 ? 32 : 64;
    // clang-format on
}

template <size_t N> struct IntegerType;

template <> struct IntegerType<8> { using type = uint8_t; };
template <> struct IntegerType<16> { using type = uint16_t; };
template <> struct IntegerType<32> { using type = uint32_t; };
template <> struct IntegerType<64> { using type = uint64_t; };

template <size_t N> struct Integer : IntegerType<next_bitsize(N)> {};
}  // namespace internal

#define WLAN_BIT_FIELD(name, offset, len)                               \
    void set_##name(::wlan::common::internal::Integer<len>::type val) { \
        this->template set_bits<offset, len>(val);                      \
    }                                                                   \
    ::wlan::common::internal::Integer<len>::type name() const {         \
        return this->template get_bits<offset, len>();                  \
    }

}  // namespace common
}  // namespace wlan
