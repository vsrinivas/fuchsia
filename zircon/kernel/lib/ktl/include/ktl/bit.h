// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_BIT_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_BIT_H_

#include <lib/stdcompat/bit.h>

namespace ktl {

using cpp20::bit_cast;
using cpp20::bit_ceil;
using cpp20::bit_floor;
using cpp20::bit_width;
using cpp20::countl_one;
using cpp20::countl_zero;
using cpp20::countr_one;
using cpp20::countr_zero;
using cpp20::endian;
using cpp20::has_single_bit;
using cpp20::popcount;
using cpp20::rotl;
using cpp20::rotr;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_BIT_H_
