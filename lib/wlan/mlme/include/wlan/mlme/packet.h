// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/wlan.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

typedef struct ethmac_netbuf ethmac_netbuf_t;

namespace wlan {

// A Buffer is a type that points at bytes and knows how big it is. For now, it can also carry
// out-of-band control data.
class Buffer {
   public:
    virtual ~Buffer() {}
    virtual uint8_t* data() = 0;
    virtual uint8_t* ctrl() = 0;
    virtual size_t capacity() const = 0;
    virtual void clear(size_t len) = 0;
};

constexpr size_t kCtrlSize = 32;

namespace internal {
template <size_t BufferSize> class FixedBuffer : public Buffer {
   public:
    uint8_t* data() override { return data_; }
    uint8_t* ctrl() override { return ctrl_; }
    size_t capacity() const override { return BufferSize; }
    void clear(size_t len) override {
        std::memset(data_, 0, std::min(BufferSize, len));
        std::memset(ctrl_, 0, kCtrlSize);
    }

   private:
    uint8_t data_[BufferSize];
    // Embedding the control data directly into the buffer is not ideal.
    // TODO(tkilbourn): replace this with a general solution.
    uint8_t ctrl_[kCtrlSize];
};
}  // namespace internal

constexpr size_t kSlabOverhead = 16;  // overhead for the slab allocator as a whole

template <size_t NumBuffers, size_t BufferSize> class SlabBuffer;
template <size_t NumBuffers, size_t BufferSize>
using SlabBufferTraits =
    fbl::StaticSlabAllocatorTraits<fbl::unique_ptr<SlabBuffer<NumBuffers, BufferSize>>,
                                   sizeof(internal::FixedBuffer<BufferSize>) * NumBuffers +
                                       kSlabOverhead>;

// A SlabBuffer is an implementation of a Buffer that comes from a fbl::SlabAllocator. The size of
// the internal::FixedBuffer and the number of buffers is part of the typename of the SlabAllocator,
// so the SlabBuffer itself is also templated on these parameters.
template <size_t NumBuffers, size_t BufferSize>
class SlabBuffer final : public internal::FixedBuffer<BufferSize>,
                         public fbl::SlabAllocated<SlabBufferTraits<NumBuffers, BufferSize>> {};

// Huge buffers are used for sending lots of data between drivers and the wlanstack.
constexpr size_t kHugeBuffers = 8;
constexpr size_t kHugeBufferSize = 16384;
// Large buffers can hold the largest 802.11 MSDU, standard Ethernet MTU,
// or HT A-MSDU of size 3,839 bytes.
constexpr size_t kLargeBuffers = 32;
constexpr size_t kLargeBufferSize = 4096;
// Small buffers are for smaller control packets within the driver stack itself and
// for transfering small 802.11 frames as well.
constexpr size_t kSmallBuffers = 512;
constexpr size_t kSmallBufferSize = 256;

using HugeBufferTraits = SlabBufferTraits<kHugeBuffers, kHugeBufferSize>;
using LargeBufferTraits = SlabBufferTraits<kLargeBuffers, kLargeBufferSize>;
using SmallBufferTraits = SlabBufferTraits<kSmallBuffers, kSmallBufferSize>;
using HugeBufferAllocator = fbl::SlabAllocator<HugeBufferTraits>;
using LargeBufferAllocator = fbl::SlabAllocator<LargeBufferTraits>;
using SmallBufferAllocator = fbl::SlabAllocator<SmallBufferTraits>;

// Gets a (slab allocated) Buffer with at least |len| bytes capacity.
fbl::unique_ptr<Buffer> GetBuffer(size_t len);

// A Packet wraps a buffer with information about the recipient/sender and length of the data
// within the buffer.
class Packet : public fbl::DoublyLinkedListable<fbl::unique_ptr<Packet>> {
   public:
    enum class Peer {
        kUnknown,
        kDevice,
        kWlan,
        kEthernet,
        kService,
    };

    static fbl::unique_ptr<Packet> CreateWlanPacket(size_t frame_len);

    Packet(fbl::unique_ptr<Buffer> buffer, size_t len);
    size_t Capacity() const { return buffer_->capacity(); }
    void clear() {
        ZX_DEBUG_ASSERT(!has_ext_data());
        buffer_->clear(len_);
        ctrl_len_ = 0;
    }

    void set_peer(Peer s) { peer_ = s; }
    Peer peer() const { return peer_; }

    const uint8_t* data() const { return buffer_->data(); }
    uint8_t* mut_data() { return buffer_->data(); }

    // Length can only be made shorter at this time.
    zx_status_t set_len(size_t len) {
        if (len > len_) return ZX_ERR_INVALID_ARGS;
        len_ = len;
        return ZX_OK;
    }
    size_t len() const { return len_; }

    template <typename T> const T* field(size_t offset) const {
        return FromBytes<T>(buffer_->data() + offset, len_ - offset);
    }

    template <typename T> T* mut_field(size_t offset) const {
        return FromBytes<T>(buffer_->data() + offset, len_ - offset);
    }

    template <typename T> bool has_ctrl_data() const { return ctrl_len_ == sizeof(T); }

    template <typename T> const T* ctrl_data() const {
        static_assert(fbl::is_standard_layout<T>::value, "Control data must have standard layout");
        static_assert(kCtrlSize >= sizeof(T),
                      "Control data type too large for Buffer ctrl_data field");
        return FromBytes<T>(buffer_->ctrl(), ctrl_len_);
    }

    template <typename T> void CopyCtrlFrom(const T& t) {
        static_assert(fbl::is_standard_layout<T>::value, "Control data must have standard layout");
        static_assert(kCtrlSize >= sizeof(T),
                      "Control data type too large for Buffer ctrl_data field");
        std::memcpy(buffer_->ctrl(), &t, sizeof(T));
        ctrl_len_ = sizeof(T);
    }

    zx_status_t CopyFrom(const void* src, size_t len, size_t offset);

    zx_status_t AsWlanTxPacket(wlan_tx_packet_t* tx_pkt);

    bool has_ext_data() const { return ext_data_ != nullptr; }
    void set_ext_data(ethmac_netbuf_t* netbuf, uint16_t offset) {
        ZX_DEBUG_ASSERT(!has_ext_data());
        ext_data_ = netbuf;
        ext_offset_ = offset;
    }
    ethmac_netbuf_t* ext_data() const { return ext_data_; }
    uint16_t ext_offset() const { return ext_offset_; }

   private:
    fbl::unique_ptr<Buffer> buffer_;
    size_t len_ = 0;
    size_t ctrl_len_ = 0;
    Peer peer_ = Peer::kUnknown;
    ethmac_netbuf_t* ext_data_ = nullptr;
    uint16_t ext_offset_ = 0;
};

class PacketQueue {
   public:
    using PacketPtr = fbl::unique_ptr<Packet>;

    bool is_empty() const { return queue_.is_empty(); }
    size_t size() const { return size_; }
    void clear() {
        queue_.clear();
        size_ = 0;
    }

    void Enqueue(PacketPtr packet) {
        ZX_DEBUG_ASSERT(packet.get() != nullptr);
        queue_.push_front(std::move(packet));
        size_++;
    }
    void UndoEnqueue() {
        ZX_DEBUG_ASSERT(!is_empty());
        queue_.pop_front();
        size_--;
    }

    PacketPtr Dequeue() {
        auto packet = queue_.pop_back();
        if (packet.get()) size_--;
        return packet;
    }

   private:
    fbl::DoublyLinkedList<PacketPtr> queue_;
    size_t size_ = 0;
};

}  // namespace wlan

// Declaration of static slab allocators.
FWD_DECL_STATIC_SLAB_ALLOCATOR(::wlan::LargeBufferTraits);
FWD_DECL_STATIC_SLAB_ALLOCATOR(::wlan::SmallBufferTraits);
