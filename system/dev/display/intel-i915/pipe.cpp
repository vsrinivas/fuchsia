// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <float.h>
#include <math.h>
#include <zircon/device/backlight.h>

#include "display-device.h"
#include "intel-i915.h"
#include "macros.h"
#include "pipe.h"
#include "registers.h"
#include "registers-dpll.h"
#include "registers-transcoder.h"
#include "tiling.h"

namespace {

uint32_t float_to_i915_csc_offset(float f) {
    ZX_DEBUG_ASSERT(0 <= f && f < 1.0f); // Controller::CheckConfiguration validates this

    // f is in [0, 1). Multiply by 2^12 to convert to a 12-bit fixed-point fraction.
    return static_cast<uint32_t>(f * pow(FLT_RADIX, 12));
}

uint32_t float_to_i915_csc_coefficient(float f) {
    registers::CscCoeffFormat res;
    if (f < 0) {
        f *= -1;
        res.set_sign(1);
    }

    if (f < .125) {
        res.set_exponent(res.kExponent0125);
        f /= .125f;
    } else if (f < .25) {
        res.set_exponent(res.kExponent025);
        f /= .25f;
    } else if (f < .5) {
        res.set_exponent(res.kExponent05);
        f /= .5f;
    } else if (f < 1) {
        res.set_exponent(res.kExponent1);
    } else if (f < 2) {
        res.set_exponent(res.kExponent2);
        f /= 2.0f;
    } else {
        res.set_exponent(res.kExponent4);
        f /= 4.0f;
    }
    f = (f * 512) + .5f;

    if (f >= 512) {
        res.set_mantissa(0x1ff);
    } else {
        res.set_mantissa(static_cast<uint16_t>(f));
    }

    return res.reg_value();
}

uint32_t encode_pipe_color_component(uint8_t component) {
    // Convert to unsigned .10 fixed point format
    return component << 2;
}

} // namespace

namespace i915 {

Pipe::Pipe(Controller* controller, registers::Pipe pipe)
        : controller_(controller), pipe_(pipe) {}

Pipe::~Pipe() {
    Reset();
}

hwreg::RegisterIo* Pipe::mmio_space() const {
    return controller_->mmio_space();
}

void Pipe::Init() {
    pipe_power_ = controller_->power()->GetPipePowerWellRef(pipe_);
    controller_->interrupts()->EnablePipeVsync(pipe_, true);
}

void Pipe::Resume() {
    controller_->interrupts()->EnablePipeVsync(pipe_, true);
}

void Pipe::Reset() {
    controller_->ResetPipe(pipe_);
    controller_->ResetTrans(transcoder());
    attached_display_ = INVALID_DISPLAY_ID;
}

void Pipe::AttachToDisplay(uint64_t id, bool is_edp) {
    attached_display_ = id;
    attached_edp_ = is_edp;
}

void Pipe::ApplyModeConfig(const display_mode_t& mode) {
    registers::TranscoderRegs trans_regs(transcoder());

    // Configure the rest of the transcoder
    uint32_t h_active = mode.h_addressable - 1;
    uint32_t h_sync_start = h_active + mode.h_front_porch;
    uint32_t h_sync_end = h_sync_start + mode.h_sync_pulse;
    uint32_t h_total = h_active + mode.h_blanking;

    uint32_t v_active = mode.v_addressable - 1;
    uint32_t v_sync_start = v_active + mode.v_front_porch;
    uint32_t v_sync_end = v_sync_start + mode.v_sync_pulse;
    uint32_t v_total = v_active + mode.v_blanking;

    auto h_total_reg = trans_regs.HTotal().FromValue(0);
    h_total_reg.set_count_total(h_total);
    h_total_reg.set_count_active(h_active);
    h_total_reg.WriteTo(mmio_space());
    auto v_total_reg = trans_regs.VTotal().FromValue(0);
    v_total_reg.set_count_total(v_total);
    v_total_reg.set_count_active(v_active);
    v_total_reg.WriteTo(mmio_space());

    auto h_sync_reg = trans_regs.HSync().FromValue(0);
    h_sync_reg.set_sync_start(h_sync_start);
    h_sync_reg.set_sync_end(h_sync_end);
    h_sync_reg.WriteTo(mmio_space());
    auto v_sync_reg = trans_regs.VSync().FromValue(0);
    v_sync_reg.set_sync_start(v_sync_start);
    v_sync_reg.set_sync_end(v_sync_end);
    v_sync_reg.WriteTo(mmio_space());

    // The Intel docs say that H/VBlank should be programmed with the same H/VTotal
    trans_regs.HBlank().FromValue(h_total_reg.reg_value()).WriteTo(mmio_space());
    trans_regs.VBlank().FromValue(v_total_reg.reg_value()).WriteTo(mmio_space());

    registers::PipeRegs pipe_regs(pipe());
    auto pipe_size = pipe_regs.PipeSourceSize().FromValue(0);
    pipe_size.set_horizontal_source_size(mode.h_addressable - 1);
    pipe_size.set_vertical_source_size(mode.v_addressable - 1);
    pipe_size.WriteTo(mmio_space());
}

void Pipe::ApplyConfiguration(const display_config_t* config) {
    ZX_ASSERT(config);

    registers::pipe_arming_regs_t regs;
    registers::PipeRegs pipe_regs(pipe_);

    if (config->cc_flags) {
        float zero_offset[3] = {};
        SetColorConversionOffsets(true, config->cc_flags & COLOR_CONVERSION_PREOFFSET ?
                config->cc_preoffsets : zero_offset);
        SetColorConversionOffsets(false, config->cc_flags & COLOR_CONVERSION_POSTOFFSET ?
                config->cc_postoffsets : zero_offset);

        float identity[3][3] = {
            { 1, 0, 0, },
            { 0, 1, 0, },
            { 0, 0, 1, },
        };
        for (uint32_t i = 0; i < 3; i++) {
            for (uint32_t j = 0; j < 3; j++) {
                float val = config->cc_flags & COLOR_CONVERSION_COEFFICIENTS ?
                        config->cc_coefficients[i][j] : identity[i][j];

                auto reg = pipe_regs.CscCoeff(i, j).ReadFrom(mmio_space());
                reg.coefficient(i, j).set( float_to_i915_csc_coefficient(val));
                reg.WriteTo(mmio_space());
            }
        }
    }
    regs.csc_mode = pipe_regs.CscMode().ReadFrom(mmio_space()).reg_value();

    auto bottom_color = pipe_regs.PipeBottomColor().FromValue(0);
    bottom_color.set_csc_enable(!!config->cc_flags);
    bool has_color_layer = config->layer_count && config->layers[0]->type == LAYER_COLOR;
    if (has_color_layer) {
        color_layer_t* layer = &config->layers[0]->cfg.color;
        ZX_DEBUG_ASSERT(layer->format == ZX_PIXEL_FORMAT_RGB_x888
                || layer->format == ZX_PIXEL_FORMAT_ARGB_8888);
        uint32_t color = *reinterpret_cast<uint32_t*>(layer->color);

        bottom_color.set_r(encode_pipe_color_component(static_cast<uint8_t>(color >> 16)));
        bottom_color.set_g(encode_pipe_color_component(static_cast<uint8_t>(color >> 8)));
        bottom_color.set_b(encode_pipe_color_component(static_cast<uint8_t>(color)));
    }
    regs.pipe_bottom_color = bottom_color.reg_value();

    bool scaler_1_claimed = false;
    for (unsigned plane = 0; plane < 3; plane++) {
        primary_layer_t* primary = nullptr;
        for (unsigned j = 0; j < config->layer_count; j++) {
            layer_t* layer = config->layers[j];
            if (layer->type == LAYER_PRIMARY && (layer->z_index - has_color_layer) == plane) {
                primary = &layer->cfg.primary;
                break;
            }
        }
        ConfigurePrimaryPlane(plane, primary, !!config->cc_flags, &scaler_1_claimed, &regs);
    }
    cursor_layer_t* cursor = nullptr;
    if (config->layer_count && config->layers[config->layer_count - 1]->type == LAYER_CURSOR) {
        cursor = &config->layers[config->layer_count - 1]->cfg.cursor;
    }
    ConfigureCursorPlane(cursor, !!config->cc_flags, &regs);

    pipe_regs.CscMode().FromValue(regs.csc_mode).WriteTo(mmio_space());
    pipe_regs.PipeBottomColor().FromValue(regs.pipe_bottom_color).WriteTo(mmio_space());
    pipe_regs.CursorBase().FromValue(regs.cur_base).WriteTo(mmio_space());
    pipe_regs.CursorPos().FromValue(regs.cur_pos).WriteTo(mmio_space());
    for (unsigned i = 0; i < registers::kImagePlaneCount; i++) {
        pipe_regs.PlaneSurface(i).FromValue(regs.plane_surf[i]).WriteTo(mmio_space());
    }
    pipe_regs.PipeScalerWinSize(0).FromValue(regs.ps_win_sz[0]).WriteTo(mmio_space());
    if (pipe_ != registers::PIPE_C) {
        pipe_regs.PipeScalerWinSize(1)
                .FromValue(regs.ps_win_sz[1]).WriteTo(mmio_space());
    }
}

void Pipe::ConfigurePrimaryPlane(uint32_t plane_num, const primary_layer_t* primary,
                                          bool enable_csc, bool* scaler_1_claimed,
                                          registers::pipe_arming_regs_t* regs) {
    registers::PipeRegs pipe_regs(pipe());

    auto plane_ctrl = pipe_regs.PlaneControl(plane_num).ReadFrom(controller_->mmio_space());
    if (primary == nullptr) {
        plane_ctrl.set_plane_enable(0).WriteTo(mmio_space());
        regs->plane_surf[plane_num] = 0;
        return;
    }

    const image_t* image = &primary->image;

    const fbl::unique_ptr<GttRegion>& region = controller_->GetGttRegion(image->handle);
    region->SetRotation(primary->transform_mode, *image);

    uint32_t plane_width;
    uint32_t plane_height;
    uint32_t stride;
    uint32_t x_offset;
    uint32_t y_offset;
    if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY
            || primary->transform_mode == FRAME_TRANSFORM_ROT_180) {
        plane_width = primary->src_frame.width;
        plane_height = primary->src_frame.height;
        stride = width_in_tiles(image->type, image->width, image->pixel_format);
        x_offset = primary->src_frame.x_pos;
        y_offset = primary->src_frame.y_pos;
    } else {
        uint32_t tile_height = height_in_tiles(image->type, image->height, image->pixel_format);
        uint32_t tile_px_height = get_tile_px_height(image->type, image->pixel_format);
        uint32_t total_height = tile_height * tile_px_height;

        plane_width = primary->src_frame.height;
        plane_height = primary->src_frame.width;
        stride = tile_height;
        x_offset = total_height - primary->src_frame.y_pos - primary->src_frame.height;
        y_offset = primary->src_frame.x_pos;
    }

    if (plane_width == primary->dest_frame.width
            && plane_height == primary->dest_frame.height) {
        auto plane_pos = pipe_regs.PlanePosition(plane_num).FromValue(0);
        plane_pos.set_x_pos(primary->dest_frame.x_pos);
        plane_pos.set_y_pos(primary->dest_frame.y_pos);
        plane_pos.WriteTo(mmio_space());

        // If there's a scaler pointed at this plane, immediately disable it
        // in case there's nothing else that will claim it this frame.
        if (scaled_planes_[pipe()][plane_num]) {
            uint32_t scaler_idx = scaled_planes_[pipe()][plane_num] - 1;
            pipe_regs.PipeScalerCtrl(scaler_idx)
                    .ReadFrom(mmio_space()).set_enable(0).WriteTo(mmio_space());
            scaled_planes_[pipe()][plane_num] = 0;
            regs->ps_win_sz[scaler_idx] = 0;
        }
    } else {
        pipe_regs.PlanePosition(plane_num).FromValue(0).WriteTo(mmio_space());

        auto ps_ctrl = pipe_regs.PipeScalerCtrl(*scaler_1_claimed).ReadFrom(mmio_space());
        ps_ctrl.set_mode(ps_ctrl.kDynamic);
        if (primary->src_frame.width > 2048) {
            float max_dynamic_height = static_cast<float>(plane_height)
                    * registers::PipeScalerCtrl::kDynamicMaxVerticalRatio2049;
            if (static_cast<uint32_t>(max_dynamic_height) < primary->dest_frame.height) {
                // TODO(stevensd): This misses some cases where 7x5 can be used.
                ps_ctrl.set_enable(ps_ctrl.k7x5);
            }
        }
        ps_ctrl.set_binding(plane_num + 1);
        ps_ctrl.set_enable(1);
        ps_ctrl.WriteTo(mmio_space());

        auto ps_win_pos = pipe_regs.PipeScalerWinPosition(*scaler_1_claimed).FromValue(0);
        ps_win_pos.set_x_pos(primary->dest_frame.x_pos);
        ps_win_pos.set_x_pos(primary->dest_frame.y_pos);
        ps_win_pos.WriteTo(mmio_space());

        auto ps_win_size = pipe_regs.PipeScalerWinSize(*scaler_1_claimed).FromValue(0);
        ps_win_size.set_x_size(primary->dest_frame.width);
        ps_win_size.set_y_size(primary->dest_frame.height);
        regs->ps_win_sz[*scaler_1_claimed] = ps_win_size.reg_value();

        scaled_planes_[pipe()][plane_num] = (*scaler_1_claimed) + 1;
        *scaler_1_claimed = true;
    }

    auto plane_size = pipe_regs.PlaneSurfaceSize(plane_num).FromValue(0);
    plane_size.set_width_minus_1(plane_width - 1);
    plane_size.set_height_minus_1(plane_height - 1);
    plane_size.WriteTo(mmio_space());

    auto plane_offset = pipe_regs.PlaneOffset(plane_num).FromValue(0);
    plane_offset.set_start_x(x_offset);
    plane_offset.set_start_y(y_offset);
    plane_offset.WriteTo(mmio_space());

    auto stride_reg = pipe_regs.PlaneSurfaceStride(plane_num).FromValue(0);
    stride_reg.set_stride(stride);
    stride_reg.WriteTo(controller_->mmio_space());

    auto plane_key_mask = pipe_regs.PlaneKeyMask(plane_num).FromValue(0);
    if (primary->alpha_mode != ALPHA_DISABLE && !isnan(primary->alpha_layer_val)) {
        plane_key_mask.set_plane_alpha_enable(1);

        uint8_t alpha = static_cast<uint8_t>(round(primary->alpha_layer_val * 255));

        auto plane_key_max = pipe_regs.PlaneKeyMax(plane_num).FromValue(0);
        plane_key_max.set_plane_alpha_value(alpha);
        plane_key_max.WriteTo(mmio_space());
    }
    plane_key_mask.WriteTo(mmio_space());
    if (primary->alpha_mode == ALPHA_DISABLE
            || primary->image.pixel_format == ZX_PIXEL_FORMAT_RGB_x888) {
        plane_ctrl.set_alpha_mode(plane_ctrl.kAlphaDisable);
    } else if (primary->alpha_mode == ALPHA_PREMULTIPLIED) {
        plane_ctrl.set_alpha_mode(plane_ctrl.kAlphaPreMultiply);
    } else {
        ZX_ASSERT(primary->alpha_mode == ALPHA_HW_MULTIPLY);
        plane_ctrl.set_alpha_mode(plane_ctrl.kAlphaHwMultiply);
    }

    plane_ctrl.set_plane_enable(1);
    plane_ctrl.set_pipe_csc_enable(enable_csc);
    plane_ctrl.set_source_pixel_format(plane_ctrl.kFormatRgb8888);
    if (primary->image.type == IMAGE_TYPE_SIMPLE) {
        plane_ctrl.set_tiled_surface(plane_ctrl.kLinear);
    } else if (primary->image.type == IMAGE_TYPE_X_TILED) {
        plane_ctrl.set_tiled_surface(plane_ctrl.kTilingX);
    } else if (primary->image.type == IMAGE_TYPE_Y_LEGACY_TILED) {
        plane_ctrl.set_tiled_surface(plane_ctrl.kTilingYLegacy);
    } else {
        ZX_ASSERT(primary->image.type == IMAGE_TYPE_YF_TILED);
        plane_ctrl.set_tiled_surface(plane_ctrl.kTilingYF);
    }
    if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY) {
        plane_ctrl.set_plane_rotation(plane_ctrl.kIdentity);
    } else if (primary->transform_mode == FRAME_TRANSFORM_ROT_90) {
        plane_ctrl.set_plane_rotation(plane_ctrl.k90deg);
    } else if (primary->transform_mode == FRAME_TRANSFORM_ROT_180) {
        plane_ctrl.set_plane_rotation(plane_ctrl.k180deg);
    } else {
        ZX_ASSERT(primary->transform_mode == FRAME_TRANSFORM_ROT_270);
        plane_ctrl.set_plane_rotation(plane_ctrl.k270deg);
    }
    plane_ctrl.WriteTo(controller_->mmio_space());

    uint32_t base_address = static_cast<uint32_t>(region->base());

    auto plane_surface = pipe_regs.PlaneSurface(plane_num).ReadFrom(controller_->mmio_space());
    plane_surface.set_surface_base_addr(base_address >> plane_surface.kRShiftCount);
    regs->plane_surf[plane_num] = plane_surface.reg_value();
}

void Pipe::ConfigureCursorPlane(const cursor_layer_t* cursor, bool enable_csc,
                                         registers::pipe_arming_regs_t* regs) {
    registers::PipeRegs pipe_regs(pipe());

    auto cursor_ctrl = pipe_regs.CursorCtrl().ReadFrom(controller_->mmio_space());
    // The hardware requires that the cursor has at least one pixel on the display,
    // so disable the plane if there is no overlap.
    if (cursor == nullptr) {
        cursor_ctrl.set_mode_select(cursor_ctrl.kDisabled).WriteTo(mmio_space());
        regs->cur_base = regs->cur_pos = 0;
        return;
    }

    if (cursor->image.width == 64) {
        cursor_ctrl.set_mode_select(cursor_ctrl.kArgb64x64);
    } else if (cursor->image.width == 128) {
        cursor_ctrl.set_mode_select(cursor_ctrl.kArgb128x128);
    } else if (cursor->image.width == 256) {
        cursor_ctrl.set_mode_select(cursor_ctrl.kArgb256x256);
    } else {
        // The configuration was not properly validated
        ZX_ASSERT(false);
    }
    cursor_ctrl.set_pipe_csc_enable(enable_csc);
    cursor_ctrl.WriteTo(mmio_space());

    auto cursor_pos = pipe_regs.CursorPos().FromValue(0);
    if (cursor->x_pos < 0) {
        cursor_pos.set_x_sign(1);
        cursor_pos.set_x_pos(-cursor->x_pos);
    } else {
        cursor_pos.set_x_pos(cursor->x_pos);
    }
    if (cursor->y_pos < 0) {
        cursor_pos.set_y_sign(1);
        cursor_pos.set_y_pos(-cursor->y_pos);
    } else {
        cursor_pos.set_y_pos(cursor->y_pos);
    }
    regs->cur_pos = cursor_pos.reg_value();

    uint32_t base_address =
            static_cast<uint32_t>(reinterpret_cast<uint64_t>(cursor->image.handle));
    auto cursor_base = pipe_regs.CursorBase().ReadFrom(controller_->mmio_space());
    cursor_base.set_cursor_base(base_address >> cursor_base.kPageShift);
    regs->cur_base = cursor_base.reg_value();
}

void Pipe::SetColorConversionOffsets(bool preoffsets, const float vals[3]) {
    registers::PipeRegs pipe_regs(pipe());

    for (uint32_t i = 0; i < 3; i++) {
        float offset = vals[i];
        auto offset_reg = pipe_regs.CscOffset(preoffsets, i).FromValue(0);
        if (offset < 0) {
            offset_reg.set_sign(1);
            offset *= -1;
        }
        offset_reg.set_magnitude(float_to_i915_csc_offset(offset));
        offset_reg.WriteTo(mmio_space());
    }
}

} // namespace i915
