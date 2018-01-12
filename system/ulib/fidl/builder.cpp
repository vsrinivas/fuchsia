// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/builder.h>

#include <string.h>

namespace fidl {

Builder::Builder(void* buffer, size_t capacity)
    : capacity_(capacity), at_(0u), buffer_(static_cast<uint8_t*>(buffer)) {
}

Builder::~Builder() = default;

void* Builder::Allocate(size_t size) {
    constexpr uint64_t alignment_mask = FIDL_ALIGNMENT - 1;
    size = (size + alignment_mask) & ~alignment_mask;
    if (capacity_ - at_ < size)
        return nullptr;
    uint8_t* result = &buffer_[at_];
    memset(result, 0, size);
    at_ += size;
    return result;
}

} // namespace fidl
