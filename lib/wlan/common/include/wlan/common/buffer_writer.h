// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstring>
#include <fbl/unique_ptr.h>
#include <wlan/common/span.h>
#include <zircon/types.h>

namespace wlan {

class BufferWriter {
   public:
    explicit BufferWriter(Span<uint8_t> buf) : buf_(buf) {
        ZX_ASSERT(buf.data() != nullptr);
    }

    void WriteByte(uint8_t byte) {
        ZX_ASSERT(buf_.size() >= offset_ + 1);
        buf_[offset_] = byte;
        offset_ += 1;
    }

    template <typename T> T* Write() {
        ZX_ASSERT(buf_.size() >= offset_ + sizeof(T));

        memset(buf_.data() + offset_, 0, sizeof(T));
        auto data = reinterpret_cast<T*>(buf_.data() + offset_);
        offset_ += sizeof(T);
        return data;
    }

    void Write(Span<const uint8_t> buf) {
        Write(as_bytes(buf));
    }

    void Write(Span<const std::byte> buf) {
        ZX_ASSERT(buf_.size() >= offset_ + buf.size());

        std::memcpy(buf_.data() + offset_, buf.data(), buf.size());
        offset_ += buf.size();
    }

    Span<const uint8_t> WrittenData() const { return buf_.subspan(0, offset_); }
    size_t WrittenBytes() const { return offset_; }
    size_t RemainingBytes() const { return buf_.size() - offset_; }

private:
    size_t offset_ = 0;
    Span<uint8_t> buf_;
};

}  // namespace wlan
