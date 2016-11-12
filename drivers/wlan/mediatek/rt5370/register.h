// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>

namespace rt5370 {

template<typename AddrType, typename ValueType, AddrType A>
class BitField {
  public:
    static_assert(std::is_unsigned<ValueType>::value,
            "BitField only supports unsigned value types.");
    static constexpr AddrType addr() { return A; }

    constexpr explicit BitField(ValueType val) : val_(val) {}
    constexpr BitField() = default;

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
};

template<uint16_t A>
class Register : public BitField<uint16_t, uint32_t, A> {
  public:
    constexpr Register() = default;
};

template<uint16_t A>
class EepromField : public BitField<uint16_t, uint16_t, A> {
  public:
    constexpr explicit EepromField(uint16_t val) : BitField<uint16_t, uint16_t, A>(val) {}
    constexpr EepromField() = default;
};

template<uint8_t A>
class BbpRegister : public BitField<uint8_t, uint8_t, A> {
  public:
    constexpr explicit BbpRegister(uint8_t val) : BitField<uint8_t, uint8_t, A>(val) {}
    constexpr BbpRegister() = default;
};

template<uint8_t A>
class RfcsrRegister : public BitField<uint8_t, uint8_t, A> {
  public:
    constexpr explicit RfcsrRegister(uint8_t val) : BitField<uint8_t, uint8_t, A>(val) {}
    constexpr RfcsrRegister() = default;
};

}  // namespace rt5370
