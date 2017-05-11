// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/types.h>
#include <mx/vmo.h>
#include <mxtl/macros.h>

namespace audio {
namespace intel_hda {

#define _SIC_ static inline constexpr
template <typename T> _SIC_ T  OR(T x, T y) { return static_cast<T>(x | y); }
template <typename T> _SIC_ T AND(T x, T y) { return static_cast<T>(x & y); }
#undef _SIC_

using WaitConditionFn = bool (*)(void*);
mx_status_t WaitCondition(mx_time_t timeout,
                          mx_time_t poll_interval,
                          WaitConditionFn cond,
                          void* cond_ctx);

// Utility method for determining the physical mapping of the pages committed
// underneath a VMO.  Automatically coalesces adjacent pages and reports the
// addresses and lengths of contiguous regions.
//
// @param vmo_handle A handle to the VMO to get region info for.
// @param vmo_size The size of the region of the VMO to get info for (in bytes).
// @param regions_out a pointer to an array of VMORegion structures which will
//        hold the result of the operation.
// @param num_regions_inout A pointer to an integer which holds the length of
//        the regions_out array on input, and hold the number of populated
//        elements of the array on output.  Only valid on output if the return
//        code is NO_ERROR.
// @returns An mx_status_t indicating success or failure of the operation.
struct VMORegion;
mx_status_t GetVMORegionInfo(const mx::vmo& vmo,
                             uint64_t       vmo_size,
                             VMORegion*     regions_out,
                             uint32_t*      num_regions_inout);
struct VMORegion {
    mx_paddr_t phys_addr;
    uint64_t   size;
};

// Utility class for managing allocation and mapping of contiguous physical
// memory.
class ContigPhysMem {
public:
    ContigPhysMem() = default;
    ~ContigPhysMem() { Release(); }

    DISALLOW_COPY_ASSIGN_AND_MOVE(ContigPhysMem);

    // Allocate at least size bytes of contiguous physical memory.  Allocatation
    // will round up to the nearest page size.
    mx_status_t Allocate(size_t size);

    // Map a successfully allocated buffer into this address space with
    // read/write permissions.
    //
    // TODO(johngro) : Should we provide control of permissions and cache policy
    // here?
    mx_status_t Map();

    // If mapped, unmap.  Then, if allocated, deallocate.
    void Release();

    mx_paddr_t  phys()        const { return phys_; }
    uintptr_t   virt()        const { return virt_; }
    size_t      size()        const { return size_; }
    size_t      actual_size() const { return actual_size_; }

private:
    mx::vmo     vmo_;
    mx_paddr_t  phys_ = 0;
    uintptr_t   virt_ = 0;
    size_t      size_ = 0;
    size_t      actual_size_ = 0;
};

struct StreamFormat {
    // Stream format bitfields documented in section 3.7.1
    static constexpr uint16_t FLAG_NON_PCM = (1u << 15);

    constexpr StreamFormat() { }
    explicit constexpr StreamFormat(uint16_t raw_data) : raw_data_(raw_data) { }

    uint32_t BASE() const { return (raw_data_ & (1u << 14)) ? 44100 : 48000; }
    uint32_t CHAN() const { return (raw_data_ & 0xF) + 1; }
    uint32_t DIV()  const { return ((raw_data_ >> 8) & 0x7) + 1; }
    uint32_t MULT() const {
        uint32_t bits = (raw_data_ >> 11) & 0x7;
        if (bits >= 4)
            return 0;
        return bits + 1;
    }
    uint32_t BITS_NDX() const { return (raw_data_ >> 4) & 0x7; }
    uint32_t BITS() const {
        switch (BITS_NDX()) {
        case 0: return 8u;
        case 1: return 16u;
        case 2: return 20u;
        case 3: return 24u;
        case 4: return 32u;
        default: return 0u;
        }
    }

    bool     is_pcm()        const { return (raw_data_ & FLAG_NON_PCM) == 0; }
    uint32_t sample_rate()   const { return (BASE() * MULT()) / DIV(); }
    uint32_t channels()      const { return CHAN(); }
    uint32_t bits_per_chan() const { return BITS(); }

    uint32_t bytes_per_frame() const {
        uint32_t ret = CHAN();
        switch (BITS_NDX()) {
        case 0: return ret;
        case 1: return ret << 1;
        case 2:
        case 3:
        case 4: return ret << 2;
        default: return 0u;
        }

    }

    bool SanityCheck() const {
        if (raw_data_ == 0x8000)
            return true;

        if (raw_data_ & 0x8080)
            return false;

        return (BITS() && MULT());
    }

    uint16_t raw_data_ = 0;
};

}  // namespace intel_hda
}  // namespace audio
