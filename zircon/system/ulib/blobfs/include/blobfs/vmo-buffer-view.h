// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <blobfs/vmo-buffer.h>

namespace blobfs {

// A wrap-around view into a portion of a VmoBuffer.
//
// Does not own the VmoBuffer. Caution must be taken when using VmoBufferView to not
// outlive the source VmoBuffer object. This is akin to a "StringView" object for a string.
//
// This class is movable and copyable.
// This class is thread-compatible.
class VmoBufferView {
public:
    VmoBufferView() = default;
    VmoBufferView(VmoBuffer* buffer, size_t start, size_t length) :
        buffer_(buffer), start_(start % buffer->capacity()), length_(length) {
        ZX_DEBUG_ASSERT(length <= buffer->capacity());
    }
    VmoBufferView(const VmoBufferView&) = default;
    VmoBufferView& operator=(const VmoBufferView&) = default;
    VmoBufferView(VmoBufferView&& other) = default;
    VmoBufferView& operator=(VmoBufferView&& other) = default;
    ~VmoBufferView() = default;

    size_t start() const { return start_; }
    size_t length() const { return length_; }
    vmoid_t vmoid() const { return buffer_ ? buffer_->vmoid() : VMOID_INVALID; }

    // Returns one block of data starting at block |index| within this view.
    void* Data(size_t index) {
        return const_cast<void*>(const_cast<const VmoBufferView*>(this)->Data(index));
    }

    const void* Data(size_t index) const {
        ZX_DEBUG_ASSERT_MSG(index < length_, "Accessing data outside the length of the view");
        return buffer_->Data((start_ + index) % buffer_->capacity());
    }

private:
    VmoBuffer* buffer_ = nullptr;
    size_t start_ = 0;
    size_t length_ = 0;
};

} // namespace blobfs
