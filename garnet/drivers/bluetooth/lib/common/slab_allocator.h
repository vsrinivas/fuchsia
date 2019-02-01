// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_SLAB_ALLOCATOR_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_SLAB_ALLOCATOR_H_

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"

namespace btlib {
namespace common {

// NOTE: Tweak these as needed.
constexpr size_t kSmallBufferSize = 64;
constexpr size_t kLargeBufferSize = 2048;

constexpr size_t kMaxNumSlabs = 100;
constexpr size_t kSlabSize = 32767;

common::MutableByteBufferPtr NewSlabBuffer(size_t size);

}  // namespace common
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_SLAB_ALLOCATOR_H_
