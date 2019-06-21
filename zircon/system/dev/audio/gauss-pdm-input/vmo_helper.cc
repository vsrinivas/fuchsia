// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <lib/zx/vmar.h>

#include "vmo_helper.h"

namespace audio {
namespace gauss {

zx_status_t VmoHelperBase::AllocateVmo(zx_handle_t bti, size_t buffer_size) {
    return io_buffer_init(&buffer_, bti, buffer_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
}

zx_status_t VmoHelper<true>::AllocateVmo(zx_handle_t bti, size_t buffer_size) {
    zx_status_t status = VmoHelperBase::AllocateVmo(bti, buffer_size);
    if (status != ZX_OK) {
        return status;
    }

    ring_buffer_virt_ = reinterpret_cast<uintptr_t>(io_buffer_virt(&buffer_));
    return status;
}

zx_status_t VmoHelperBase::GetVmoRange(zx_paddr_t* start_address) {
    *start_address = io_buffer_phys(&buffer_);
    return ZX_OK;
}

zx_status_t VmoHelperBase::Duplicate(uint32_t rights, zx::vmo* handle) {
    zx_handle_t copy;
    zx_status_t status = zx_handle_duplicate(buffer_.vmo_handle, rights, &copy);
    if (status != ZX_OK) {
        return status;
    }
    handle->reset(copy);
    return ZX_OK;
}

void VmoHelperBase::DestroyVmo() {
    io_buffer_release(&buffer_);
}

template <bool DEBUG>
zx_status_t VmoHelper<DEBUG>::AllocateVmo(zx_handle_t bti, size_t buffer_size) {
    return VmoHelperBase::AllocateVmo(bti, buffer_size);
}

template <bool DEBUG>
void VmoHelper<DEBUG>::printoffsetinvmo(uint32_t offset) {}

void VmoHelper<true>::printoffsetinvmo(uint32_t offset) {
    io_buffer_cache_flush_invalidate(&buffer_, 0, buffer_.size);

    zxlogf(DEBUG1, "Current position: 0x%04x. data: ", offset);

    // Print some offsets to probe the data.
    static const uint32_t offsets[] = {0, 0x1000, 0x2000, 0x3000,
                                       0x4000, 0x5000, 0x6000, 0x7000};

    for (const auto& offset : offsets) {
        zxlogf(DEBUG1, " 0x%04x: 0x%08lx,", offset,
               *(reinterpret_cast<uintptr_t*>(offset +
                                              ring_buffer_virt_)));
    }

    // print the last frame of data:
    zxlogf(DEBUG1, "offset is at: 0x%x\n", offset);

    if (offset > 32) {
        uint8_t* frame_start = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(offset) - 32 + ring_buffer_virt_);
        for (int i = 0; i < 32; i++) {
            zxlogf(DEBUG1, "%d: 0x%x, ", i, (frame_start[i] & 0xff));
        }
    }

    zxlogf(DEBUG1, "\n");
}

template <bool DEBUG>
void VmoHelper<DEBUG>::DestroyVmo() {
    VmoHelperBase::DestroyVmo();
}

void VmoHelper<true>::DestroyVmo() {
    VmoHelperBase::DestroyVmo();
    ring_buffer_virt_ = 0;
}

template class VmoHelper<false>;
}
}
