// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>

#include "display-device.h"
#include "intel-i915.h"
#include "macros.h"
#include "registers.h"
#include "registers-dpll.h"
#include "registers-transcoder.h"

namespace i915 {

DisplayDevice::DisplayDevice(Controller* controller, uint64_t id,
                             registers::Ddi ddi, registers::Trans trans, registers::Pipe pipe)
        : controller_(controller), id_(id), ddi_(ddi), trans_(trans), pipe_(pipe) {}

DisplayDevice::~DisplayDevice() {
    if (inited_) {
        ResetPipe();
        ResetTrans();
        ResetDdi();
    }
}

hwreg::RegisterIo* DisplayDevice::mmio_space() const {
    return controller_->mmio_space();
}

void DisplayDevice::ResetPipe() {
    controller_->ResetPipe(pipe_);
}

bool DisplayDevice::ResetTrans() {
    return controller_->ResetTrans(trans_);
}

bool DisplayDevice::ResetDdi() {
    return controller_->ResetDdi(ddi_);
}

bool DisplayDevice::Init() {
    ddi_power_ = controller_->power()->GetDdiPowerWellRef(ddi_);
    pipe_power_ = controller_->power()->GetPipePowerWellRef(pipe_);

    if (!QueryDevice(&edid_)) {
        return false;
    }

    edid::timing_params_t preferred_timing;
    if (!edid_.GetPreferredTiming(&preferred_timing)) {
        return false;
    }

    info_.pixel_clock_10khz = preferred_timing.pixel_freq_10khz;
    info_.h_addressable = preferred_timing.horizontal_addressable;
    info_.h_front_porch = preferred_timing.horizontal_front_porch;
    info_.h_sync_pulse = preferred_timing.horizontal_sync_pulse;
    info_.h_blanking = preferred_timing.horizontal_blanking;
    info_.v_addressable = preferred_timing.vertical_addressable;
    info_.v_front_porch = preferred_timing.vertical_front_porch;
    info_.v_sync_pulse = preferred_timing.vertical_sync_pulse;
    info_.v_blanking = preferred_timing.vertical_blanking;
    info_.mode_flags = (preferred_timing.vertical_sync_pulse ? MODE_FLAG_VSYNC_POSITIVE : 0)
            | (preferred_timing.horizontal_sync_pulse ? MODE_FLAG_HSYNC_POSITIVE : 0)
            | (preferred_timing.interlaced ? MODE_FLAG_INTERLACED : 0);

    ResetPipe();
    if (!ResetTrans() || !ResetDdi()) {
        return false;
    }

    if (!ConfigureDdi()) {
        return false;
    }

    controller_->interrupts()->EnablePipeVsync(pipe_, true);

    inited_ = true;

    return true;
}

bool DisplayDevice::Resume() {
    if (!ConfigureDdi()) {
        return false;
    }

    controller_->interrupts()->EnablePipeVsync(pipe_, true);

    return true;
}

void DisplayDevice::ApplyConfiguration(const display_config_t* config) {
    if (config == nullptr) {
        ResetPipe();
        return;
    }

    if (memcmp(&config->mode, &info_, sizeof(display_mode_t)) != 0) {
        ResetPipe();
        ResetTrans();
        ResetDdi();

        info_ = config->mode;

        ConfigureDdi();
    }

    registers::PipeRegs pipe_regs(pipe());

    auto pipe_size = pipe_regs.PipeSourceSize().FromValue(0);
    pipe_size.set_horizontal_source_size(info_.h_addressable - 1);
    pipe_size.set_vertical_source_size(info_.v_addressable - 1);
    pipe_size.WriteTo(mmio_space());

    for (unsigned i = 0; i < 3; i++) {
        primary_layer_t* primary = nullptr;
        for (unsigned j = 0; j < config->layer_count; j++) {
            layer_t* layer = config->layers[j];
            if (layer->z_index == i) {
                primary = &layer->cfg.primary;
                break;
            }
        }
        if (!primary) {
            auto plane_ctrl = pipe_regs.PlaneControl(i).ReadFrom(controller_->mmio_space());
            plane_ctrl.set_plane_enable(0);
            plane_ctrl.WriteTo(controller_->mmio_space());

            auto plane_surface = pipe_regs.PlaneSurface(i).ReadFrom(controller_->mmio_space());
            plane_surface.set_surface_base_addr(0);
            plane_surface.WriteTo(controller_->mmio_space());
            continue;
        }

        auto plane_size = pipe_regs.PlaneSurfaceSize(i).FromValue(0);
        plane_size.set_width_minus_1(primary->dest_frame.width - 1);
        plane_size.set_height_minus_1(primary->dest_frame.height - 1);
        plane_size.WriteTo(mmio_space());

        auto plane_pos = pipe_regs.PlanePosition(i).FromValue(0);
        plane_pos.set_x_pos(primary->dest_frame.x_pos);
        plane_pos.set_y_pos(primary->dest_frame.y_pos);
        plane_pos.WriteTo(mmio_space());

        auto plane_offset = pipe_regs.PlaneOffset(i).FromValue(0);
        plane_offset.set_start_x(primary->src_frame.x_pos);
        plane_offset.set_start_y(primary->src_frame.y_pos);
        plane_offset.WriteTo(mmio_space());

        auto stride_reg = pipe_regs.PlaneSurfaceStride(i).FromValue(0);
        stride_reg.set_stride(primary->image.type,
                              primary->image.width, primary->image.pixel_format);
        stride_reg.WriteTo(controller_->mmio_space());

        auto plane_ctrl = pipe_regs.PlaneControl(i).ReadFrom(controller_->mmio_space());
        plane_ctrl.set_plane_enable(1);
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
        plane_ctrl.WriteTo(controller_->mmio_space());

        uint32_t base_address =
                static_cast<uint32_t>(reinterpret_cast<uint64_t>(primary->image.handle));

        auto plane_surface = pipe_regs.PlaneSurface(i).ReadFrom(controller_->mmio_space());
        plane_surface.set_surface_base_addr(base_address >> plane_surface.kRShiftCount);
        plane_surface.WriteTo(controller_->mmio_space());
    }
}

} // namespace i915
