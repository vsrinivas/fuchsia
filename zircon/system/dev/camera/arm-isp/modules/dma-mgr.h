// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "../pingpong_regs.h"
#include "dma-format.h"
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-pool.h>

namespace camera {

class DmaManager {
public:
    DmaManager(bool is_downscaled)
        : downscaled_(is_downscaled){}

    // TODO(garratt): fill out this function.
    zx_status_t ConnectToStream(fuchsia_sysmem_BufferCollectionInfo buffer_collection);
    zx_status_t SetFrameRate(uint32_t fps);

    // Called when the first pixel reaches the input_port, which is the first
    // component of the ISP.
    void OnNewFrame();

    // Signal that all consumers are done with this frame.
    void ReleaseFrame(uint32_t buffer_index);

private:
    ddk::MmioBuffer* isp_mmio_;
    fzl::VmoPool buffers_;
    uint32_t fps_;
    DmaFormat current_format_;
    bool downscaled_ = false;
    fit::function<void(uint32_t buffer_index)> publish_buffer_callback_;

    // Get the Registers used by the DMA Writer.
    auto GetPrimaryMisc();
    auto GetUvMisc();
    auto GetPrimaryBank0();
    auto GetUvBank0();
    auto GetPrimaryActiveDim();
    auto GetUvActiveDim();
    auto GetPrimaryLineOffset();
    auto GetUvLineOffset();

    // Writes the dma format to the registers
    void SetFormat(DmaFormat format);
};

} // namespace camera
