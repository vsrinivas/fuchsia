// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <wlan/mlme/packet.h>
#include <zircon/types.h>

namespace wlan {

class BufferWriter {
   public:
    BufferWriter(uint8_t* buf, size_t len) : buf_(buf), len_(len) {
        ZX_ASSERT(buf != nullptr);
    }

    explicit BufferWriter(Packet* pkt) : buf_(pkt->mut_data()), len_(pkt->len()) {
        ZX_ASSERT(pkt != nullptr);
    }

    template <typename T> T* Write() {
        ZX_ASSERT(len_ >= offset_ + sizeof(T));

        memset(buf_ + offset_, 0, sizeof(T));
        auto data = reinterpret_cast<T*>(buf_ + offset_);
        offset_ += sizeof(T);
        return data;
    }

    void Write(const uint8_t* buf, size_t len) {
        ZX_ASSERT(len_ >= offset_ + len);

        std::memcpy(buf_ + offset_, buf, len);
        offset_ += len;
    }

    size_t WrittenBytes() const { return offset_; }
    size_t RemainingBytes() const { return len_ - offset_; }

private:
    size_t offset_ = 0;
    uint8_t* buf_;
    size_t len_ = 0;
};

}  // namespace wlan
