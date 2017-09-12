// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <stdint.h>

namespace hwreg {

namespace internal {

template <typename T> struct IsSupportedInt : fbl::false_type {};
template <> struct IsSupportedInt<uint8_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint16_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint32_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint64_t> : fbl::true_type {};

} // namespace internal

} // namespace hwreg
