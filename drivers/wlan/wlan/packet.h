// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "wlan.h"

#include <magenta/types.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/slab_allocator.h>
#include <mxtl/unique_ptr.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wlan {

// A Buffer is a type that points at bytes and knows how big it is. For now, it can also carry
// out-of-band control data.
class Buffer {
  public:
    virtual ~Buffer() {}
    virtual uint8_t* data() = 0;
    virtual uint8_t* ctrl() = 0;
    virtual size_t capacity() const = 0;
};

constexpr size_t kCtrlSize = 32;

namespace internal {
template <size_t BufferSize>
class FixedBuffer : public Buffer {
  public:
    uint8_t* data() override { return data_; }
    uint8_t* ctrl() override { return ctrl_; }
    size_t capacity() const override { return BufferSize; }

  private:
    uint8_t data_[BufferSize];
    // Embedding the control data directly into the buffer is not ideal.
    // TODO(tkilbourn): replace this with a general solution.
    uint8_t ctrl_[kCtrlSize];
};
}  // namespace internal

constexpr size_t kSlabOverhead = 16;  // overhead for the slab allocator as a whole

template <size_t NumBuffers, size_t BufferSize>
class SlabBuffer;
template <size_t NumBuffers, size_t BufferSize>
using SlabBufferTraits =
    mxtl::StaticSlabAllocatorTraits<mxtl::unique_ptr<SlabBuffer<NumBuffers, BufferSize>>,
        sizeof(internal::FixedBuffer<BufferSize>) * NumBuffers + kSlabOverhead>;

// A SlabBuffer is an implementation of a Buffer that comes from a mxtl::SlabAllocator. The size of
// the internal::FixedBuffer and the number of buffers is part of the typename of the SlabAllocator,
// so the SlabBuffer itself is also templated on these parameters.
template <size_t NumBuffers, size_t BufferSize>
class SlabBuffer final : public internal::FixedBuffer<BufferSize>,
                         public mxtl::SlabAllocated<SlabBufferTraits<NumBuffers, BufferSize>> {};

// Large buffers can hold the largest 802.11 MSDU or standard Ethernet MTU.
constexpr size_t kLargeBuffers = 32;
constexpr size_t kLargeBufferSize = 2560;
// Small buffers are for smaller control packets within the driver stack itself (though they could
// be used for transfering small 802.11 frames as well).
constexpr size_t kSmallBuffers = 1024;
constexpr size_t kSmallBufferSize = 64;

using LargeBufferTraits = SlabBufferTraits<kLargeBuffers, kLargeBufferSize>;
using SmallBufferTraits = SlabBufferTraits<kSmallBuffers, kSmallBufferSize>;
using LargeBufferAllocator = mxtl::SlabAllocator<LargeBufferTraits>;
using SmallBufferAllocator = mxtl::SlabAllocator<SmallBufferTraits>;

// A Packet wraps a buffer with information about the recipient/sender and length of the data
// within the buffer.
class Packet : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<Packet>> {
  public:
    enum class Peer {
        kUnknown,
        kWlan,
        kEthernet,
        kService,
    };

    Packet(mxtl::unique_ptr<Buffer> buffer, size_t len);
    size_t Capacity() const { return buffer_->capacity(); }

    void set_peer(Peer s) { peer_ = s; }
    Peer peer() const { return peer_; }

    const uint8_t* data() const { return buffer_->data(); }
    uint8_t* mut_data() { return buffer_->data(); }
    size_t len() const { return len_; }

    template <typename T>
    const T* field(size_t offset) const {
        return FromBytes<T>(buffer_->data() + offset, len_ - offset);
    }

    template <typename T>
    bool has_ctrl_data() const {
        return ctrl_len_ == sizeof(T);
    }

    template <typename T>
    const T* ctrl_data() const {
        static_assert(mxtl::is_standard_layout<T>::value, "Control data must have standard layout");
        static_assert(kCtrlSize >= sizeof(T),
                      "Control data type too large for Buffer ctrl_data field");
        return FromBytes<T>(buffer_->ctrl(), ctrl_len_);
    }

    template <typename T>
    void CopyCtrlFrom(const T& t) {
        static_assert(mxtl::is_standard_layout<T>::value, "Control data must have standard layout");
        static_assert(kCtrlSize >= sizeof(T),
                      "Control data type too large for Buffer ctrl_data field");
        std::memcpy(buffer_->ctrl(), &t, sizeof(T));
        ctrl_len_ = sizeof(T);
    }

    mx_status_t CopyFrom(const void* src, size_t len, size_t offset);

  private:
    mxtl::unique_ptr<Buffer> buffer_;
    size_t len_ = 0;
    size_t ctrl_len_ = 0;
    Peer peer_ = Peer::kUnknown;
};

}  // namespace wlan

// Declaration of static slab allocators.
FWD_DECL_STATIC_SLAB_ALLOCATOR(::wlan::LargeBufferTraits);
FWD_DECL_STATIC_SLAB_ALLOCATOR(::wlan::SmallBufferTraits);
