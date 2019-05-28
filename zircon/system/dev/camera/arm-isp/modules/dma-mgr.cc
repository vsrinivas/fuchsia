// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dma-mgr.h"

#include "../pingpong_regs.h"
#include "dma-format.h"
#include <cstdint>

namespace camera {

auto DmaManager::GetPrimaryMisc() {
    if (downscaled_) {
        return ping::DownScaled::Primary::DmaWriter_Misc::Get();
    } else {
        return ping::FullResolution::Primary::DmaWriter_Misc::Get();
    }
}

auto DmaManager::GetUvMisc() {
    if (downscaled_) {
        return ping::DownScaled::Uv::DmaWriter_Misc::Get();
    } else {
        return ping::FullResolution::Uv::DmaWriter_Misc::Get();
    }
}

auto DmaManager::GetPrimaryBank0() {
    if (downscaled_) {
        return ping::DownScaled::Primary::DmaWriter_Bank0Base::Get();
    } else {
        return ping::FullResolution::Primary::DmaWriter_Bank0Base::Get();
    }
}

auto DmaManager::GetUvBank0() {
    if (downscaled_) {
        return ping::DownScaled::Uv::DmaWriter_Bank0Base::Get();
    } else {
        return ping::FullResolution::Uv::DmaWriter_Bank0Base::Get();
    }
}

auto DmaManager::GetPrimaryLineOffset() {
    if (downscaled_) {
        return ping::DownScaled::Primary::DmaWriter_LineOffset::Get();
    } else {
        return ping::FullResolution::Primary::DmaWriter_LineOffset::Get();
    }
}

auto DmaManager::GetUvLineOffset() {
    if (downscaled_) {
        return ping::DownScaled::Uv::DmaWriter_LineOffset::Get();
    } else {
        return ping::FullResolution::Uv::DmaWriter_LineOffset::Get();
    }
}

auto DmaManager::GetPrimaryActiveDim() {
    if (downscaled_) {
        return ping::DownScaled::Primary::DmaWriter_ActiveDim::Get();
    } else {
        return ping::FullResolution::Primary::DmaWriter_ActiveDim::Get();
    }
}

auto DmaManager::GetUvActiveDim() {
    if (downscaled_) {
        return ping::DownScaled::Uv::DmaWriter_ActiveDim::Get();
    } else {
        return ping::FullResolution::Uv::DmaWriter_ActiveDim::Get();
    }
}

// Called as one of the later steps when a new frame arrives.
void DmaManager::OnNewFrame() {
    // 1) Publish last frame
    if (buffers_.HasBufferInProgress()) {
        uint32_t buffer_index;
        buffers_.BufferCompleted(&buffer_index);
        if (publish_buffer_callback_) {
            publish_buffer_callback_(buffer_index);
        }
    }
    // 2) Get another buffer
    buffers_.GetNewBuffer();
    // 3) Optional?  Set the DMA settings again... seems unnecessary
    // 4) Set the DMA address
    uint32_t memory_address = static_cast<uint32_t>(
                              reinterpret_cast<uintptr_t>(buffers_.CurrentBufferAddress()));
    // clang-format off
    GetPrimaryBank0().FromValue(0)
      .set_value(memory_address + current_format_.GetBank0Offset())
      .WriteTo(&isp_mmio_local_);
    if (current_format_.HasSecondaryChannel()) {
        GetUvBank0().FromValue(0)
          .set_value(memory_address + current_format_.GetBank0OffsetUv())
          .WriteTo(&isp_mmio_local_);
    }
    // 5) Optional? Enable Write_on
    GetPrimaryMisc().ReadFrom(&isp_mmio_local_)
        .set_frame_write_on(1)
        .WriteTo(&isp_mmio_local_);
    if (current_format_.HasSecondaryChannel()) {
        GetUvMisc().ReadFrom(&isp_mmio_local_)
            .set_frame_write_on(1)
            .WriteTo(&isp_mmio_local_);
    }
   // clang-format on
}

void DmaManager::ReleaseFrame(uint32_t buffer_index) {
    buffers_.BufferRelease(buffer_index);
}

void DmaManager::SetFormat(DmaFormat format) {
    current_format_ = format;
    // Write format to registers
    // clang-format off
    GetPrimaryMisc().ReadFrom(&isp_mmio_local_)
        .set_base_mode(format.GetBaseMode())
        .set_plane_select(format.GetPlaneSelect())
        .WriteTo(&isp_mmio_local_);
    GetPrimaryActiveDim().ReadFrom(&isp_mmio_local_)
        .set_active_width(format.width_)
        .set_active_height(format.height_)
        .WriteTo(&isp_mmio_local_);
    GetPrimaryLineOffset().ReadFrom(&isp_mmio_local_)
        .set_value(format.GetLineOffset())
        .WriteTo(&isp_mmio_local_);
    if (format.HasSecondaryChannel()) {
        // TODO: should there be a format.WidthUv() ?
        GetUvMisc().ReadFrom(&isp_mmio_local_)
            .set_base_mode(format.GetBaseMode())
            .set_plane_select(format.GetPlaneSelect())
            .WriteTo(&isp_mmio_local_);
        GetUvActiveDim().ReadFrom(&isp_mmio_local_)
            .set_active_width(format.width_)
            .set_active_height(format.height_)
            .WriteTo(&isp_mmio_local_);
        GetUvLineOffset().ReadFrom(&isp_mmio_local_)
            .set_value(format.GetLineOffset())
            .WriteTo(&isp_mmio_local_);
    }
    // clang-format on
}
} // namespace camera
