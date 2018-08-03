// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/usb.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zircon/device/usb.h>
#include <zircon/hw/usb.h>
#include <zircon/types.h>
#include <zircon/syscalls.h>

#include <threads.h>
#include <sys/types.h>

namespace audio {
namespace usb {

// A small helper class for managing a device's descriptor list memory.
class DescriptorListMemory : public fbl::RefCounted<DescriptorListMemory> {
  public:
    class Iterator {
      public:
        explicit Iterator(fbl::RefPtr<DescriptorListMemory> mem);

        // Advance the iterator to the next valid header and return true.  If
        // there are no more valid headers, simply return false.
        bool Next();

        bool valid() const { return (offset_ < mem_->size()); }
        size_t offset() const { return offset_; }
        const fbl::RefPtr<DescriptorListMemory>& desc_list() const { return mem_; }

        const usb_descriptor_header_t* hdr() const {
            if (!valid()) {
                return nullptr;
            }

            auto tmp = reinterpret_cast<uintptr_t>(mem_->data());
            return reinterpret_cast<const usb_descriptor_header_t*>(tmp + offset_);
        }

        template <typename T>
        const T* hdr_as() const {
            auto h = hdr();
            ZX_DEBUG_ASSERT(offset_ <=  mem_->size());
            return ((h != nullptr) && (h->bLength <= (mem_->size() - offset_)))
                ? reinterpret_cast<const T*>(h)
                : nullptr;
        }

      private:
        DISALLOW_COPY_ASSIGN_AND_MOVE(Iterator);

        // Validate that the current offset points at something which could be a
        // valid descriptor which fits within the descriptor memory and return
        // true.  Otherwise, invalidate the offset and return false.
        bool ValidateOffset();

        fbl::RefPtr<DescriptorListMemory> mem_;
        size_t offset_ = 0;
    };

    static fbl::RefPtr<DescriptorListMemory> Create(usb_protocol_t* proto);
    const void* data() const { return data_; }
    size_t      size() const { return size_; }

  private:
    friend class fbl::RefPtr<DescriptorListMemory>;

    DescriptorListMemory() = default;
    ~DescriptorListMemory();

    void*  data_ = nullptr;
    size_t size_ = 0;
};

}  // namespace usb
}  // namespace audio
