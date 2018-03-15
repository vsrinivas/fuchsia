// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/builder.h>

#include <string.h>

#include <lib/fidl/internal.h>

namespace fidl {

Builder::Builder() : capacity_(0u), at_(0u), buffer_(nullptr) {}

Builder::Builder(void* buffer, uint32_t capacity)
    : capacity_(capacity), at_(0u), buffer_(static_cast<uint8_t*>(buffer)) {
}

Builder::~Builder() = default;

void* Builder::Allocate(uint32_t size) {
    uint64_t limit = FidlAlign(at_ + size);
    if (limit > capacity_)
        return nullptr;
    uint8_t* result = &buffer_[at_];
    memset(buffer_ + at_, 0, limit - at_);
    at_ = static_cast<uint32_t>(limit);
    return result;
}

BytePart Builder::Finalize() {
    BytePart bytes(buffer_, capacity_, at_);
    capacity_ = 0u;
    at_ = 0u;
    return bytes;
}

void Builder::Reset(void* buffer, uint32_t capacity) {
    buffer_ = static_cast<uint8_t*>(buffer);
    capacity_ = capacity;
    at_ = 0u;
}

} // namespace fidl
