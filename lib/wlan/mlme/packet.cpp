// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/packet.h>

#include <zircon/assert.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace wlan {

Packet::Packet(fbl::unique_ptr<Buffer> buffer, size_t len) : buffer_(std::move(buffer)), len_(len) {
    ZX_ASSERT(buffer_.get());
    ZX_DEBUG_ASSERT(len <= buffer_->capacity());
}

zx_status_t Packet::CopyFrom(const void* src, size_t len, size_t offset) {
    if (offset + len > buffer_->capacity()) { return ZX_ERR_BUFFER_TOO_SMALL; }
    std::memcpy(buffer_->data() + offset, src, len);
    len_ = std::max(len_, offset + len);
    return ZX_OK;
}

wlan_tx_packet_t Packet::AsWlanTxPacket() {
    ZX_DEBUG_ASSERT(len() <= std::numeric_limits<uint16_t>::max());
    wlan_tx_packet_t tx_pkt = {};
    tx_pkt.packet_head.data_buffer = data();
    tx_pkt.packet_head.data_size = static_cast<uint16_t>(len());
    if (has_ext_data()) {
        tx_pkt.packet_tail = ext_data();
        tx_pkt.tail_offset = ext_offset();
    }
    if (has_ctrl_data<wlan_tx_info_t>()) {
        std::memcpy(&tx_pkt.info, ctrl_data<wlan_tx_info_t>(), sizeof(tx_pkt.info));
    }
    return tx_pkt;
}

bool IsBodyAligned(const Packet& pkt) {
    auto rx = pkt.ctrl_data<wlan_rx_info_t>();
    return rx != nullptr && rx->rx_flags & WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4;
}

rust_mlme_buffer_t IntoRustMlmeBuffer(fbl::unique_ptr<Packet> packet) {
    auto* pkt = packet.release();
    return rust_mlme_buffer_t {
            .data = pkt->data(),
            .len = pkt->len(),
            .raw = pkt,
            .return_buffer = [](void *raw) {
                fbl::unique_ptr<Packet>(static_cast<Packet *>(raw)).reset();
            },
    };
}

void LogAllocationFail(Buffer::Size size) {
    BufferDebugger<SmallBufferAllocator, LargeBufferAllocator, HugeBufferAllocator,
                   kBufferDebugEnabled>::Fail(size);
}

fbl::unique_ptr<Buffer> GetBuffer(size_t len) {
    fbl::unique_ptr<Buffer> buffer;

    if (len <= kSmallBufferSize) {
        buffer = SmallBufferAllocator::New();
        if (buffer != nullptr) {
            return buffer;
        } else {
            LogAllocationFail(Buffer::Size::kSmall);
        }
    }

    if (len <= kLargeBufferSize) {
        buffer = LargeBufferAllocator::New();
        if (buffer != nullptr) {
            return buffer;
        } else {
            LogAllocationFail(Buffer::Size::kLarge);
        }
    }

    if (len <= kHugeBufferSize) {
        buffer = HugeBufferAllocator::New();
        if (buffer != nullptr) {
            return buffer;
        } else {
            LogAllocationFail(Buffer::Size::kHuge);
        }
    }

    return nullptr;
}

fbl::unique_ptr<Packet> GetPacket(size_t len, Packet::Peer peer) {
    auto buffer = GetBuffer(len);
    if (buffer == nullptr) { return nullptr; }
    auto packet = fbl::make_unique<Packet>(std::move(buffer), len);
    packet->set_peer(peer);
    return packet;
}

fbl::unique_ptr<Packet> GetEthPacket(size_t len) {
    return GetPacket(len, Packet::Peer::kEthernet);
}

fbl::unique_ptr<Packet> GetWlanPacket(size_t len) {
    return GetPacket(len, Packet::Peer::kWlan);
}

fbl::unique_ptr<Packet> GetSvcPacket(size_t len) {
    return GetPacket(len, Packet::Peer::kService);
}

}  // namespace wlan

// Definition of static slab allocators.
// TODO(tkilbourn): tune how many slabs we are willing to grow up to. Reasonably large limits chosen
// for now.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::wlan::HugeBufferTraits, ::wlan::kHugeSlabs, true);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::wlan::LargeBufferTraits, ::wlan::kLargeSlabs, true);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::wlan::SmallBufferTraits, ::wlan::kSmallSlabs, true);
