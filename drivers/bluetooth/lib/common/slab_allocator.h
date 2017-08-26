// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"

namespace bluetooth {
namespace common {

// NOTE: Tweak these as needed.
constexpr size_t kSmallBufferSize = 64;
constexpr size_t kLargeBufferSize = 2048;

std::unique_ptr<common::MutableByteBuffer> NewSlabBuffer(size_t size);

}  // namespace common
}  // namespace bluetooth
