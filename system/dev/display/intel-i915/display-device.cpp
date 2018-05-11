// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

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

    if (!DoModeset()) {
        return false;
    }

    inited_ = true;

    return true;
}

bool DisplayDevice::Resume() {
    if (!DoModeset()) {
        return false;
    }

    if (is_enabled_) {
        controller_->interrupts()->EnablePipeVsync(pipe_, true);
    }

    return true;
}

void DisplayDevice::ApplyConfiguration(display_config_t* config) {
    bool enabled = config != nullptr;
    if (enabled != is_enabled_) {
        controller_->interrupts()->EnablePipeVsync(pipe_, enabled);
        is_enabled_ = enabled;
    }
    if (!is_enabled_) {
        return;
    }

    registers::PipeRegs pipe_regs(pipe());

    image_type_ = config->image.type;

    auto stride_reg = pipe_regs.PlaneSurfaceStride().FromValue(0);
    stride_reg.set_stride(config->image.type, config->image.width, config->image.pixel_format);
    stride_reg.WriteTo(controller_->mmio_space());

    auto plane_ctrl = pipe_regs.PlaneControl().ReadFrom(controller_->mmio_space());
    if (config->image.type == IMAGE_TYPE_SIMPLE) {
        plane_ctrl.set_tiled_surface(plane_ctrl.kLinear);
    } else if (config->image.type == IMAGE_TYPE_X_TILED) {
        plane_ctrl.set_tiled_surface(plane_ctrl.kTilingX);
    } else if (config->image.type == IMAGE_TYPE_Y_LEGACY_TILED) {
        plane_ctrl.set_tiled_surface(plane_ctrl.kTilingYLegacy);
    } else {
        ZX_ASSERT(config->image.type == IMAGE_TYPE_YF_TILED);
        plane_ctrl.set_tiled_surface(plane_ctrl.kTilingYF);
    }
    plane_ctrl.WriteTo(controller_->mmio_space());

    uint32_t base_address = static_cast<uint32_t>(reinterpret_cast<uint64_t>(config->image.handle));

    auto plane_surface = pipe_regs.PlaneSurface().ReadFrom(controller_->mmio_space());
    plane_surface.set_surface_base_addr(base_address >> plane_surface.kRShiftCount);
    plane_surface.WriteTo(controller_->mmio_space());
}

} // namespace i915
