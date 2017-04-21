// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_PIPE_H
#define REGISTERS_PIPE_H

#include "register_bitfields.h"

// Registers for controlling the pipes, including planes (which are part of
// pipes).

namespace registers {

class Pipe {
public:
    // Number of pipes that the hardware provides.
    static constexpr uint32_t kPipeCount = 3;
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.601
class DisplayPlaneSurfaceAddress : public RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x7019C;

    // This field omits the lower 12 bits of the address, so the address
    // must be 4k-aligned.
    static constexpr uint32_t kPageShift = 12;
    DEF_FIELD(31, 12, surface_base_address);

    DEF_BIT(3, ring_flip_source);

    // Get the instance of this register for Plane 1 of the given pipe.
    static auto Get(uint32_t pipe_number)
    {
        DASSERT(pipe_number < Pipe::kPipeCount);
        return RegisterAddr<DisplayPlaneSurfaceAddress>(kBaseAddr + 0x1000 * pipe_number);
    }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.598
class DisplayPlaneSurfaceStride : public RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x70188;

    DEF_FIELD(9, 0, stride);

    // Get the instance of this register for Plane 1 of the given pipe.
    static auto Get(uint32_t pipe_number)
    {
        DASSERT(pipe_number < Pipe::kPipeCount);
        return RegisterAddr<DisplayPlaneSurfaceStride>(kBaseAddr + 0x1000 * pipe_number);
    }
};

class DisplayPlaneSurfaceSize : public RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x70190;

    DEF_FIELD(27, 16, height_minus_1);
    DEF_FIELD(12, 0, width_minus_1);

    // Get the instance of this register for Plane 1 of the given pipe.
    static auto Get(uint32_t pipe_number)
    {
        DASSERT(pipe_number < Pipe::kPipeCount);
        return RegisterAddr<DisplayPlaneSurfaceSize>(kBaseAddr + 0x1000 * pipe_number);
    }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.559-566
class DisplayPlaneControl : public RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x70180;

    DEF_BIT(31, plane_enable);
    DEF_BIT(30, pipe_gamma_enable);
    DEF_BIT(29, remove_yuv_offset);
    DEF_BIT(28, yuv_range_correction_disable);
    DEF_FIELD(27, 24, source_pixel_format);
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
    enum Tiling { TILING_NONE = 0, TILING_X = 1, TILING_Y_LEGACY = 4, TILING_YF = 5 };

    DEF_BIT(9, async_address_update_enable);
    DEF_FIELD(7, 6, stereo_surface_vblank_mask);
    DEF_FIELD(5, 4, alpha_mode);
    DEF_BIT(3, allow_double_buffer_update_disable);
    DEF_FIELD(1, 0, plane_rotation);

    // Get the instance of this register for Plane 1 of the given pipe.
    static auto Get(uint32_t pipe_number)
    {
        DASSERT(pipe_number < Pipe::kPipeCount);
        return RegisterAddr<DisplayPlaneControl>(kBaseAddr + 0x1000 * pipe_number);
    }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf p.444
class DisplayPipeInterrupt {
public:
    enum Pipe { PIPE_A };

    static constexpr uint32_t kMaskOffsetPipeA = 0x44404;
    static constexpr uint32_t kIdentityOffsetPipeA = 0x44408;
    static constexpr uint32_t kEnableOffsetPipeA = 0x4440C;
    static constexpr uint32_t kPlane1FlipDoneBit = 1 << 3;

    static void write_mask(RegisterIo* reg_io, Pipe pipe, uint32_t bits, bool enable)
    {
        uint32_t offset, val;
        switch (pipe) {
            case PIPE_A:
                offset = kMaskOffsetPipeA;
                break;
        }

        val = reg_io->Read32(offset);
        val = enable ? (val & ~bits) : val | bits;
        reg_io->Write32(offset, val);
    }

    static void write_enable(RegisterIo* reg_io, Pipe pipe, uint32_t bits, bool enable)
    {
        uint32_t offset, val;
        switch (pipe) {
            case PIPE_A:
                offset = kEnableOffsetPipeA;
                break;
        }

        val = reg_io->Read32(offset);
        val = enable ? (val | bits) : (val & ~bits);
        reg_io->Write32(offset, val);
    }

    static void process_identity_bits(RegisterIo* reg_io, Pipe pipe, uint32_t bits,
                                      bool* bits_present_out)
    {
        uint32_t offset, val;
        switch (pipe) {
            case PIPE_A:
                offset = kIdentityOffsetPipeA;
                break;
        }
        val = reg_io->Read32(offset);
        if ((*bits_present_out = val & bits))
            reg_io->Write32(offset, val | bits); // reset the event
    }
};

} // namespace

#endif // REGISTERS_PIPE_H
