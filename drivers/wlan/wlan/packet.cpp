// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet.h"

#include <magenta/assert.h>

#include <algorithm>
#include <utility>

namespace wlan {

Packet::Packet(mxtl::unique_ptr<Buffer> buffer, size_t len)
  : buffer_(std::move(buffer)), len_(len) {
    MX_ASSERT(buffer_.get());
    MX_DEBUG_ASSERT(len <= kBufferSize);
}

void Packet::CopyFrom(const void* src, size_t len, size_t offset) {
    MX_ASSERT(offset + len <= kBufferSize);
    std::memcpy(buffer_->data + offset, src, len);
    len_ = std::max(len_, offset + len);
}

}  // namespace wlan
