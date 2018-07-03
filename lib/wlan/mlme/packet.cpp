// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/packet.h>

#include <fbl/limits.h>
#include <zircon/assert.h>

#include <algorithm>
#include <utility>

namespace wlan {

fbl::unique_ptr<Packet> Packet::CreateWlanPacket(size_t frame_len) {
    fbl::unique_ptr<Buffer> buffer = GetBuffer(frame_len);
    if (buffer == nullptr) { return nullptr; }

    auto packet = fbl::make_unique<Packet>(std::move(buffer), frame_len);
    packet->set_peer(Packet::Peer::kWlan);
    return packet;
}

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

zx_status_t Packet::AsWlanTxPacket(wlan_tx_packet_t* tx_pkt) {
    ZX_DEBUG_ASSERT(len() <= fbl::numeric_limits<uint16_t>::max());
    ethmac_netbuf_t netbuf = {
        .data = mut_data(),
        .len = static_cast<uint16_t>(len()),
    };
    *tx_pkt = {.packet_head = &netbuf};
    if (has_ext_data()) {
        tx_pkt->packet_tail = ext_data();
        tx_pkt->tail_offset = ext_offset();
    }
    if (has_ctrl_data<wlan_tx_info_t>()) {
        std::memcpy(&tx_pkt->info, ctrl_data<wlan_tx_info_t>(), sizeof(tx_pkt->info));
    }
    return ZX_OK;
}

fbl::unique_ptr<Buffer> GetBuffer(size_t len) {
    fbl::unique_ptr<Buffer> buffer;

    if (len <= kSmallBufferSize) {
        buffer = SmallBufferAllocator::New();
        if (buffer != nullptr) { return buffer; }
    }

    if (len <= kLargeBufferSize) {
        buffer = LargeBufferAllocator::New();
        if (buffer != nullptr) { return buffer; }
    }

    if (len <= kHugeBufferSize) {
        buffer = HugeBufferAllocator::New();
        if (buffer != nullptr) { return buffer; }
    }

    return nullptr;
}

}  // namespace wlan

// Definition of static slab allocators.
// TODO(tkilbourn): tune how many slabs we are willing to grow up to. Reasonably large limits chosen
// for now.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::wlan::HugeBufferTraits, ::wlan::kHugeSlabs, true);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::wlan::LargeBufferTraits, ::wlan::kLargeSlabs, true);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::wlan::SmallBufferTraits, ::wlan::kSmallSlabs, true);
