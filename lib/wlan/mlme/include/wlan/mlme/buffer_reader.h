// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <wlan/mlme/packet.h>
#include <zircon/types.h>

namespace wlan {

class BufferReader {
   public:
    BufferReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {
        ZX_ASSERT(buf != nullptr);
    }

    explicit BufferReader(const Packet* pkt) : buf_(pkt->data()), len_(pkt->len()) {
        ZX_ASSERT(pkt != nullptr);
    }

    template <typename T> const T* Peek() {
        if (len_ < offset_ + sizeof(T)) { return nullptr; }
        return reinterpret_cast<const T*>(buf_ + offset_);
    }

    template <typename T> const T* Read() {
        if (len_ < offset_ + sizeof(T)) { return nullptr; }

        auto data = reinterpret_cast<const T*>(buf_ + offset_);
        offset_ += sizeof(T);
        return data;
    }

    const uint8_t* Read(size_t len) {
        if (len_ < offset_ + len) { return nullptr; }

        auto data = buf_ + offset_;
        offset_ += len;
        return data;
    }

    std::tuple<const uint8_t*, size_t> ReadRemaining() {
        size_t remaining = RemainingBytes();
        auto data = buf_ + offset_;
        offset_ += remaining;
        return {data, remaining};
    }

    size_t ReadBytes() const { return offset_; }
    size_t RemainingBytes() const { return len_ - offset_; }

   private:
    size_t offset_ = 0;
    const uint8_t* buf_;
    size_t len_ = 0;
};

}  // namespace wlan
