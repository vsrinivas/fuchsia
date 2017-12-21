// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <limits.h>
#include <stdint.h>

namespace hwreg {

template <typename T> class RegisterBase;

namespace internal {

template <typename T> struct IsSupportedInt : fbl::false_type {};
template <> struct IsSupportedInt<uint8_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint16_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint32_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint64_t> : fbl::true_type {};

template <class IntType> constexpr IntType ComputeMask(uint32_t num_bits) {
    if (num_bits == sizeof(IntType) * CHAR_BIT) {
        return static_cast<IntType>(~0ull);
    }
    return static_cast<IntType>((static_cast<IntType>(1) << num_bits) - 1);
}

template <class IntType> class RsvdZField {
public:
    RsvdZField(RegisterBase<IntType>* reg, uint32_t bit_high_incl, uint32_t bit_low) {
        IntType mask = static_cast<IntType>(
                internal::ComputeMask<IntType>(bit_high_incl - bit_low + 1) << bit_low);
        reg->rsvdz_mask_ = static_cast<IntType>(reg->rsvdz_mask_ | mask);
    }
};

} // namespace internal

} // namespace hwreg
