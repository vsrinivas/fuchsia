// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/pixelformat.h>

namespace registers {

// Number of pipes that the hardware provides.
static constexpr uint32_t kPipeCount = 3;

enum Pipe { PIPE_A, PIPE_B, PIPE_C };

static const Pipe kPipes[kPipeCount] = {
    PIPE_A, PIPE_B, PIPE_C,
};

// PIPE_SRCSZ
class PipeSourceSize : public hwreg::RegisterBase<PipeSourceSize, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x6001c;

    DEF_FIELD(28, 16, horizontal_source_size);
    DEF_FIELD(11, 0, vertical_source_size);
};


// PLANE_SURF
class PlaneSurface : public hwreg::RegisterBase<PlaneSurface, uint32_t> {
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
class PlaneSurfaceStride : public hwreg::RegisterBase<PlaneSurfaceStride, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70188;

    DEF_FIELD(9, 0, stride);
    void set_linear_stride(uint32_t stride, zx_pixel_format_t format) {
        set_stride(stride / (kLinearStrideChunkSize / ZX_PIXEL_FORMAT_BYTES(format)));
    }

    static uint32_t compute_linear_stride(uint32_t width, zx_pixel_format_t format) {
        ZX_ASSERT(kLinearStrideChunkSize % ZX_PIXEL_FORMAT_BYTES(format) == 0);
        return fbl::round_up(width, kLinearStrideChunkSize / ZX_PIXEL_FORMAT_BYTES(format));
    }

private:
    static constexpr uint32_t kLinearStrideChunkSize = 64;
};

// PLANE_SIZE
class PlaneSurfaceSize : public hwreg::RegisterBase<PlaneSurfaceSize, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70190;

    DEF_FIELD(27, 16, height_minus_1);
    DEF_FIELD(12, 0, width_minus_1);
};

// PLANE_CTL
class PlaneControl : public hwreg::RegisterBase<PlaneControl, uint32_t> {
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
class PlaneBufCfg : public hwreg::RegisterBase<PlaneBufCfg, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x7017c;

    DEF_FIELD(25, 16, buffer_end);
    DEF_FIELD(9, 0, buffer_start);
};

// PLANE_WM
class PlaneWm : public hwreg::RegisterBase<PlaneWm, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70240;

    DEF_BIT(31, enable);
    DEF_FIELD(18, 14, lines);
    DEF_FIELD(9, 0, blocks);
};

// PS_CTRL
class PipeScalerCtrl : public hwreg::RegisterBase<PipeScalerCtrl, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x68180;

    DEF_BIT(31, enable);
};

// PS_WIN_SIZE
class PipeScalerWinSize : public hwreg::RegisterBase<PipeScalerWinSize, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x68174;

    DEF_FIELD(28, 16, x_size);
    DEF_FIELD(11, 0, y_size);
};

// DE_PIPE_INTERRUPT
class PipeDeInterrupt : public hwreg::RegisterBase<PipeDeInterrupt, uint32_t> {
public:
    DEF_BIT(1, vsync);
};

// An instance of PipeRegs represents the registers for a particular pipe.
class PipeRegs {
public:
    static constexpr uint32_t kStatusReg = 0x44400;
    static constexpr uint32_t kMaskReg = 0x44404;
    static constexpr uint32_t kIdentityReg = 0x44408;
    static constexpr uint32_t kEnableReg = 0x4440c;

    PipeRegs(Pipe pipe) : pipe_(pipe) { }

    hwreg::RegisterAddr<registers::PipeSourceSize> PipeSourceSize() {
        return GetReg<registers::PipeSourceSize>();
    }

    // The following methods get the instance of the plane register for plane 1.
    hwreg::RegisterAddr<registers::PlaneSurface> PlaneSurface() {
        return GetReg<registers::PlaneSurface>();
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceStride> PlaneSurfaceStride() {
        return GetReg<registers::PlaneSurfaceStride>();
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceSize> PlaneSurfaceSize() {
        return GetReg<registers::PlaneSurfaceSize>();
    }
    hwreg::RegisterAddr<registers::PlaneControl> PlaneControl() {
        return GetReg<registers::PlaneControl>();
    }
    // 0 == cursor, 1-3 are regular planes
    hwreg::RegisterAddr<registers::PlaneBufCfg> PlaneBufCfg(int plane) {
        return hwreg::RegisterAddr<registers::PlaneBufCfg>(
                PlaneBufCfg::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane);
    }

    hwreg::RegisterAddr<registers::PlaneWm>PlaneWatermark(int wm_num) {
        return hwreg::RegisterAddr<PlaneWm>(PlaneWm::kBaseAddr + 0x1000 * pipe_ + 4 * wm_num);
    }

    hwreg::RegisterAddr<registers::PipeScalerCtrl> PipeScalerCtrl(int num) {
        return hwreg::RegisterAddr<registers::PipeScalerCtrl>(
                PipeScalerCtrl::kBaseAddr + 0x800 * pipe_ + num * 0x100);
    }

    hwreg::RegisterAddr<registers::PipeScalerWinSize> PipeScalerWinSize(int num) {
        return hwreg::RegisterAddr<registers::PipeScalerWinSize>(
                PipeScalerWinSize::kBaseAddr + 0x800 * pipe_ + num * 0x100);
    }

    hwreg::RegisterAddr<registers::PipeDeInterrupt> PipeDeInterrupt(uint32_t type) {
        return hwreg::RegisterAddr<registers::PipeDeInterrupt>(type + 0x10 * pipe_);
    }

private:
    template <class RegType> hwreg::RegisterAddr<RegType> GetReg() {
        return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_);
    }

    Pipe pipe_;
};

} // namespace registers
