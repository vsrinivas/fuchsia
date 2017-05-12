// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/intrusive_double_list.h>
#include <mxtl/slab_allocator.h>
#include <mxtl/unique_ptr.h>

#include <cstddef>
#include <cstdint>

namespace wlan {

constexpr size_t kBufferSize = 2560;
namespace internal {
struct Buffer {
    uint8_t data[kBufferSize];
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
    size_t len() const { return len_; }

    template <typename T>
    const T* field(size_t offset) const {
        if (offset + sizeof(T) > len_) return nullptr;
        return reinterpret_cast<const T*>(buffer_->data + offset);
    }

    void CopyFrom(const void* src, size_t len, size_t offset);

  private:
    mxtl::unique_ptr<Buffer> buffer_;
    size_t len_ = 0;
    Source src_ = Source::kUnknown;
};

}  // namespace wlan
