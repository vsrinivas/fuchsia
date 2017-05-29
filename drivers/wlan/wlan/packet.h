// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/intrusive_double_list.h>
#include <mxtl/slab_allocator.h>
#include <mxtl/unique_ptr.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wlan {

constexpr size_t kBufferSize = 2560;
constexpr size_t kCtrlSize = 32;
namespace internal {
struct Buffer {
    uint8_t data[kBufferSize];
    // Embedding the control data directly into the buffer is not ideal.
    // TODO(tkilbourn): replace this with a general solution.
    uint8_t ctrl[kCtrlSize];
};
}  // namespace internal

constexpr size_t kNumBuffers = 16;
constexpr size_t kBufferObjSize = sizeof(internal::Buffer) + 8;  // 8 bytes overhead for slabs
constexpr size_t kSlabOverhead = 16;  // overhead for the slab allocator as a whole
constexpr size_t kSlabSize = kBufferObjSize * kNumBuffers + kSlabOverhead;

struct Buffer;
using BufferAllocatorTraits =
    mxtl::SlabAllocatorTraits<mxtl::unique_ptr<Buffer>, kSlabSize>;

struct Buffer : public internal::Buffer, public mxtl::SlabAllocated<BufferAllocatorTraits> {};

static_assert(sizeof(Buffer) == kBufferObjSize, "unexpected Buffer size");
static_assert(mxtl::SlabAllocator<BufferAllocatorTraits>::AllocsPerSlab >= kNumBuffers,
              "unexpected number of Buffers per slab");

class Packet : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<Packet>> {
  public:
    enum class Source {
        kUnknown,
        kWlan,
        kEthernet,
        kService,
    };

    Packet(mxtl::unique_ptr<Buffer> buffer, size_t len);
    size_t Capacity() const { return kBufferSize; }

    void set_src(Source s) { src_ = s; }
    Source src() const { return src_; }

    const uint8_t* data() const { return buffer_->data; }
    uint8_t* mut_data() { return buffer_->data; }
    size_t len() const { return len_; }

    template <typename T>
    const T* field(size_t offset) const {
        if (offset + sizeof(T) > len_) return nullptr;
        return reinterpret_cast<const T*>(buffer_->data + offset);
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
        if (ctrl_len_ < sizeof(T)) return nullptr;
        return reinterpret_cast<const T*>(buffer_->ctrl);
    }

    template <typename T>
    void CopyCtrlFrom(const T& t) {
        static_assert(mxtl::is_standard_layout<T>::value, "Control data must have standard layout");
        static_assert(kCtrlSize >= sizeof(T),
                      "Control data type too large for Buffer ctrl_data field");
        std::memcpy(buffer_->ctrl, &t, sizeof(T));
        ctrl_len_ = sizeof(T);
    }

    void CopyFrom(const void* src, size_t len, size_t offset);

  private:
    mxtl::unique_ptr<Buffer> buffer_;
    size_t len_ = 0;
    size_t ctrl_len_ = 0;
    Source src_ = Source::kUnknown;
};

}  // namespace wlan
