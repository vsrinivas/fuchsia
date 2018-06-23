// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/protocol/display-controller.h>
#include <ddk/protocol/intel-gpu-core.h>
#include <hwreg/bitfields.h>
#include <zircon/pixelformat.h>

namespace registers {

// Number of pipes that the hardware provides.
static constexpr uint32_t kPipeCount = 3;

enum Pipe { PIPE_A, PIPE_B, PIPE_C };

static const Pipe kPipes[kPipeCount] = {
    PIPE_A, PIPE_B, PIPE_C,
};

static constexpr uint32_t kImagePlaneCount = 3;
static constexpr uint32_t kCursorPlane = 2;

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

    DEF_BIT(3, ring_flip_source);
};

// PLANE_SURFLIVE
class PlaneSurfaceLive : public hwreg::RegisterBase<PlaneSurfaceLive, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x701ac;

    // This field omits the lower 12 bits of the address, so the address
    // must be 4k-aligned.
    static constexpr uint32_t kPageShift = 12;
    DEF_FIELD(31, 12, surface_base_addr);
};

// PLANE_STRIDE
class PlaneSurfaceStride : public hwreg::RegisterBase<PlaneSurfaceStride, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70188;

    DEF_FIELD(9, 0, stride);
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
    static constexpr uint32_t kAlphaDisable = 0;
    static constexpr uint32_t kAlphaPreMultiply = 2;
    static constexpr uint32_t kAlphaHwMultiply = 3;


    DEF_BIT(3, allow_double_buffer_update_disable);
    DEF_FIELD(1, 0, plane_rotation);
    static constexpr uint32_t kIdentity = 0;
    static constexpr uint32_t k90deg = 1;
    static constexpr uint32_t k180deg = 2;
    static constexpr uint32_t k270deg = 3;
};

// PLANE_BUF_CFG
class PlaneBufCfg : public hwreg::RegisterBase<PlaneBufCfg, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x7017c;
    static constexpr uint32_t kBufferCount = 892;

    DEF_FIELD(25, 16, buffer_end);
    DEF_FIELD(9, 0, buffer_start);
};

// PLANE_WM
class PlaneWm : public hwreg::RegisterBase<PlaneWm, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70140;

    DEF_BIT(31, enable);
    DEF_FIELD(18, 14, lines);
    DEF_FIELD(9, 0, blocks);
};

// PLANE_KEYMSK
class PlaneKeyMask : public hwreg::RegisterBase<PlaneKeyMask, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70198;

    DEF_BIT(31, plane_alpha_enable);
};

// PLANE_KEYMAX
class PlaneKeyMax : public hwreg::RegisterBase<PlaneKeyMax, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x701a0;

    DEF_FIELD(31, 24, plane_alpha_value);
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

// PLANE_OFFSET
class PlaneOffset : public hwreg::RegisterBase<PlaneOffset, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x701a4;

    DEF_FIELD(28, 16, start_y);
    DEF_FIELD(12, 0, start_x);
};

// PLANE_POS
class PlanePosition : public hwreg::RegisterBase<PlanePosition, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x7018c;

    DEF_FIELD(28, 16, y_pos);
    DEF_FIELD(12, 0, x_pos);
};

// CUR_BASE
class CursorBase : public hwreg::RegisterBase<CursorBase, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70084;

    DEF_FIELD(31, 12, cursor_base);
    // This field omits the lower 12 bits of the address, so the address
    // must be 4k-aligned.
    static constexpr uint32_t kPageShift = 12;
};

// CUR_CTL
class CursorCtrl : public hwreg::RegisterBase<CursorCtrl, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70080;

    DEF_BIT(24, pipe_csc_enable);
    DEF_FIELD(5, 0, mode_select);
    static constexpr uint32_t kDisabled = 0;
    static constexpr uint32_t kArgb128x128 = 34;
    static constexpr uint32_t kArgb256x256 = 35;
    static constexpr uint32_t kArgb64x64 = 39;
};

// CUR_POS
class CursorPos : public hwreg::RegisterBase<CursorPos, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70088;

    DEF_BIT(31, y_sign);
    DEF_FIELD(27, 16, y_pos);
    DEF_BIT(15, x_sign);
    DEF_FIELD(12, 0, x_pos);
};

// CUR_SURFLIVE
class CursorSurfaceLive : public hwreg::RegisterBase<CursorSurfaceLive, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x700ac;

    static constexpr uint32_t kPageShift = 12;
    DEF_FIELD(31, 12, surface_base_addr);
};

// CSC_COEFF
class CscCoeff : public hwreg::RegisterBase<CscCoeff, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x49010;

    hwreg::BitfieldRef<uint32_t> coefficient(uint32_t i, uint32_t j) {
        ZX_DEBUG_ASSERT(i < 3 && j < 3);
        uint32_t bit = 16 - ((j % 2) * 16);
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 15, bit);
    }
};

class CscCoeffFormat : public hwreg::RegisterBase<CscCoeffFormat, uint16_t> {
public:
    DEF_BIT(15, sign);
    DEF_FIELD(14, 12, exponent);
    static constexpr uint16_t kExponent0125 = 3;
    static constexpr uint16_t kExponent025 = 2;
    static constexpr uint16_t kExponent05 = 1;
    static constexpr uint16_t kExponent1 = 0;
    static constexpr uint16_t kExponent2 = 7;
    static constexpr uint16_t kExponent4 = 6;
    DEF_FIELD(11, 3, mantissa);
};

// CSC_MODE
class CscMode : public hwreg::RegisterBase<CscMode, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x49028;
};

// CSC_POSTOFF / CSC_PREOFF
class CscOffset : public hwreg::RegisterBase<CscOffset, uint32_t> {
public:
    static constexpr uint32_t kPostOffsetBaseAddr = 0x49040;
    static constexpr uint32_t kPreOffsetBaseAddr = 0x49030;

    DEF_BIT(12, sign);
    DEF_FIELD(11, 0, magnitude);
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

    hwreg::RegisterAddr<registers::PlaneSurface> PlaneSurface(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurface>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceLive> PlaneSurfaceLive(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurfaceLive>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceStride> PlaneSurfaceStride(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurfaceStride>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceSize> PlaneSurfaceSize(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurfaceSize>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneControl> PlaneControl(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneControl>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneOffset> PlaneOffset(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneOffset>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlanePosition> PlanePosition(int32_t plane_num) {
        return GetPlaneReg<registers::PlanePosition>(plane_num);
    }
    // 0 == cursor, 1-3 are regular planes
    hwreg::RegisterAddr<registers::PlaneBufCfg> PlaneBufCfg(int plane) {
        return hwreg::RegisterAddr<registers::PlaneBufCfg>(
                PlaneBufCfg::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane);
    }

    hwreg::RegisterAddr<registers::PlaneWm>PlaneWatermark(int plane, int wm_num) {
        return hwreg::RegisterAddr<PlaneWm>(
                PlaneWm::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane + 4 * wm_num);
    }

    hwreg::RegisterAddr<registers::PlaneKeyMask> PlaneKeyMask(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneKeyMask>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneKeyMax> PlaneKeyMax(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneKeyMax>(plane_num);
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

    hwreg::RegisterAddr<registers::CursorBase> CursorBase() {
        return GetReg<registers::CursorBase>();
    }

    hwreg::RegisterAddr<registers::CursorCtrl> CursorCtrl() {
        return GetReg<registers::CursorCtrl>();
    }

    hwreg::RegisterAddr<registers::CursorPos> CursorPos() {
        return GetReg<registers::CursorPos>();
    }

    hwreg::RegisterAddr<registers::CursorSurfaceLive> CursorSurfaceLive() {
        return GetReg<registers::CursorSurfaceLive>();
    }

    hwreg::RegisterAddr<registers::CscCoeff> CscCoeff(uint32_t i, uint32_t j) {
        ZX_DEBUG_ASSERT(i < 3 && j < 3);
        uint32_t base = registers::CscCoeff::kBaseAddr + 4 * ((i * 2) + (j == 2 ? 1 : 0));
        return GetCscReg<registers::CscCoeff>(base);
    }

    hwreg::RegisterAddr<registers::CscMode> CscMode() {
        return GetCscReg<registers::CscMode>(registers::CscMode::kBaseAddr);
    }

    hwreg::RegisterAddr<registers::CscOffset> CscOffset(bool preoffset, uint32_t component_idx) {
        uint32_t base = (4 * component_idx) + (preoffset ?
            registers::CscOffset::kPreOffsetBaseAddr : registers::CscOffset::kPostOffsetBaseAddr);
        return GetCscReg<registers::CscOffset>(base);
    }

private:
    template <class RegType> hwreg::RegisterAddr<RegType> GetReg() {
        return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_);
    }

    template <class RegType> hwreg::RegisterAddr<RegType> GetPlaneReg(int32_t plane) {
        return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane);
    }

    template <class RegType> hwreg::RegisterAddr<RegType> GetCscReg(uint32_t base) {
        return hwreg::RegisterAddr<RegType>(base + 0x100 * pipe_);
    }

    Pipe pipe_;
};

} // namespace registers
