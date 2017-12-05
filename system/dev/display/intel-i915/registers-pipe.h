// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "registers-base.h"

namespace registers {

// Number of pipes that the hardware provides.
static constexpr uint32_t kPipeCount = 3;

enum Pipe { PIPE_A, PIPE_B, PIPE_C };

static const Pipe kPipes[kPipeCount] = {
    PIPE_A, PIPE_B, PIPE_C,
};

// PIPE_SRCSZ
class PipeSourceSize : public RegisterBase<PipeSourceSize> {
public:
    static constexpr uint32_t kBaseAddr = 0x6001c;

    DEF_FIELD(28, 16, horizontal_source_size);
    DEF_FIELD(11, 0, vertical_source_size);
};


// PLANE_SURF
class PlaneSurface : public RegisterBase<PlaneSurface> {
public:
    static constexpr uint32_t kBaseAddr = 0x7019c;

    // This field omits the lower 12 bits of the address, so the address
    // must be 4k-aligned.
    static constexpr uint32_t kPageShift = 12;
    DEF_FIELD(31, 12, surface_base_addr);
    static constexpr uint32_t kRShiftCount = 12;
    static constexpr uint32_t kLinearAlignment = 256 * 1024;
    static constexpr uint32_t kXTilingAlignment = 256 * 1024;
    static constexpr uint32_t kYTilingAlignment = 1024 * 1024;
    static constexpr uint32_t kTrailingPtePadding = 136;
    static constexpr uint32_t kHeaderPtePaddingFor180Or270 = 136;

    DEF_BIT(3, ring_flip_source);
};

// PLANE_STRIDE
class PlaneSurfaceStride : public RegisterBase<PlaneSurfaceStride> {
public:
    static constexpr uint32_t kBaseAddr = 0x70188;

    DEF_FIELD(9, 0, stride);
    // TODO(ZX-1413): this should be 64 bytes, not 16 4-byte pixels
    static constexpr uint32_t kLinearStrideChunkSize = 16;
};

// PLANE_SIZE
class PlaneSurfaceSize : public RegisterBase<PlaneSurfaceSize> {
public:
    static constexpr uint32_t kBaseAddr = 0x70190;

    DEF_FIELD(27, 16, height_minus_1);
    DEF_FIELD(12, 0, width_minus_1);
};

// PLANE_CTL
class PlaneControl : public RegisterBase<PlaneControl> {
public:
    static constexpr uint32_t kBaseAddr = 0x70180;

    DEF_BIT(31, plane_enable);
    DEF_BIT(30, pipe_gamma_enable);
    DEF_BIT(29, remove_yuv_offset);
    DEF_BIT(28, yuv_range_correction_disable);

    DEF_FIELD(27, 24, source_pixel_format);
    static constexpr uint32_t kFormatRgb8888 = 4;

    DEF_BIT(23, pipe_csc_enable);
    DEF_FIELD(22, 21, key_enable);
    DEF_BIT(20, rgb_color_order);
    DEF_BIT(19, plane_yuv_to_rgb_csc_dis);
    DEF_BIT(18, plane_yuv_to_rgb_csc_format);
    DEF_FIELD(17, 16, yuv_422_byte_order);
    DEF_BIT(15, render_decompression);
    DEF_BIT(14, trickle_feed_enable);
    DEF_BIT(13, plane_gamma_disable);

    DEF_FIELD(12, 10, tiled_surface);
    static constexpr uint32_t kLinear = 0;
    static constexpr uint32_t kTilingX = 1;
    static constexpr uint32_t kTilingYLegacy = 4;
    static constexpr uint32_t kTilingYF = 5;

    DEF_BIT(9, async_address_update_enable);
    DEF_FIELD(7, 6, stereo_surface_vblank_mask);
    DEF_FIELD(5, 4, alpha_mode);
    DEF_BIT(3, allow_double_buffer_update_disable);
    DEF_FIELD(1, 0, plane_rotation);
};

// PLANE_BUF_CFG
class PlaneBufCfg : public RegisterBase<PlaneBufCfg> {
public:
    static constexpr uint32_t kBaseAddr = 0x7017c;

    DEF_FIELD(25, 16, buffer_end);
    DEF_FIELD(9, 0, buffer_start);
};

// PLANE_WM
class PlaneWm : public RegisterBase<PlaneWm> {
public:
    static constexpr uint32_t kBaseAddr = 0x70240;

    DEF_BIT(31, enable);
    DEF_FIELD(18, 14, lines);
    DEF_FIELD(9, 0, blocks);
};

// An instance of PipeRegs represents the registers for a particular pipe.
class PipeRegs {
public:
    PipeRegs(Pipe pipe) : pipe_(pipe) { }

    RegisterAddr<registers::PipeSourceSize> PipeSourceSize() {
        return GetReg<registers::PipeSourceSize>();
    }

    // The following methods get the instance of the plane register for plane 1.
    RegisterAddr<registers::PlaneSurface> PlaneSurface() {
        return GetReg<registers::PlaneSurface>();
    }
    RegisterAddr<registers::PlaneSurfaceStride> PlaneSurfaceStride() {
        return GetReg<registers::PlaneSurfaceStride>();
    }
    RegisterAddr<registers::PlaneSurfaceSize> PlaneSurfaceSize() {
        return GetReg<registers::PlaneSurfaceSize>();
    }
    RegisterAddr<registers::PlaneControl> PlaneControl() {
        return GetReg<registers::PlaneControl>();
    }
    // 0 == cursor, 1-3 are regular planes
    RegisterAddr<registers::PlaneBufCfg> PlaneBufCfg(int plane) {
        return RegisterAddr<registers::PlaneBufCfg>(
                PlaneBufCfg::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane);
    }

    RegisterAddr<registers::PlaneWm>PlaneWatermark(int wm_num) {
        return RegisterAddr<PlaneWm>(PlaneWm::kBaseAddr + 0x1000 * pipe_ + 4 * wm_num);
    }

private:
    template <class RegType> RegisterAddr<RegType> GetReg() {
        return RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_);
    }

    Pipe pipe_;
};

} // namespace registers
