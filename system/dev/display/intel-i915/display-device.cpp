// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <zircon/device/backlight.h>

#include "display-device.h"
#include "intel-i915.h"
#include "macros.h"
#include "registers.h"
#include "registers-dpll.h"
#include "registers-transcoder.h"
#include "tiling.h"

namespace {

zx_status_t backlight_ioctl(void* ctx, uint32_t op,
                            const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual) {
    if (op == IOCTL_BACKLIGHT_SET_BRIGHTNESS
            || op == IOCTL_BACKLIGHT_GET_BRIGHTNESS) {
        auto ref = static_cast<i915::display_ref_t*>(ctx);
        fbl::AutoLock lock(&ref->mtx);

        if (ref->display_device == NULL) {
            return ZX_ERR_PEER_CLOSED;
        }

        if (op == IOCTL_BACKLIGHT_SET_BRIGHTNESS) {
            if (in_len != sizeof(backlight_state_t) || out_len != 0) {
                return ZX_ERR_INVALID_ARGS;
            }

            const auto args = static_cast<const backlight_state_t*>(in_buf);
            ref->display_device->SetBacklightState(args->on, args->brightness);
            return ZX_OK;
        } else if (op == IOCTL_BACKLIGHT_GET_BRIGHTNESS) {
            if (out_len != sizeof(backlight_state_t) || in_len != 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            
            auto args = static_cast<backlight_state_t*>(out_buf);
            ref->display_device->GetBacklightState(&args->on, &args->brightness);
            *out_actual = sizeof(backlight_state_t);
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_SUPPORTED;
}

void backlight_release(void* ctx) {
    delete static_cast<i915::display_ref_t*>(ctx);
}

static zx_protocol_device_t backlight_ops = {};

} // namespace

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
    if (display_ref_) {
        fbl::AutoLock lock(&display_ref_->mtx);
        device_remove(backlight_device_);
        display_ref_->display_device = nullptr;
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


    auto preferred_timing = edid_.begin();
    if (!(preferred_timing != edid_.end())) {
        return false;
    }

    info_.pixel_clock_10khz = (*preferred_timing).pixel_freq_10khz;
    info_.h_addressable = (*preferred_timing).horizontal_addressable;
    info_.h_front_porch = (*preferred_timing).horizontal_front_porch;
    info_.h_sync_pulse = (*preferred_timing).horizontal_sync_pulse;
    info_.h_blanking = (*preferred_timing).horizontal_blanking;
    info_.v_addressable = (*preferred_timing).vertical_addressable;
    info_.v_front_porch = (*preferred_timing).vertical_front_porch;
    info_.v_sync_pulse = (*preferred_timing).vertical_sync_pulse;
    info_.v_blanking = (*preferred_timing).vertical_blanking;
    info_.mode_flags = ((*preferred_timing).vertical_sync_pulse ? MODE_FLAG_VSYNC_POSITIVE : 0)
            | ((*preferred_timing).horizontal_sync_pulse ? MODE_FLAG_HSYNC_POSITIVE : 0)
            | ((*preferred_timing).interlaced ? MODE_FLAG_INTERLACED : 0);

    ResetPipe();
    if (!ResetTrans() || !ResetDdi()) {
        return false;
    }

    if (!ConfigureDdi()) {
        return false;
    }

    controller_->interrupts()->EnablePipeVsync(pipe_, true);

    inited_ = true;

    if (HasBacklight()) {
        fbl::AllocChecker ac;
        auto display_ref = fbl::make_unique_checked<display_ref_t>(&ac);
        zx_status_t status = ZX_ERR_NO_MEMORY;
        if (ac.check()) {
            mtx_init(&display_ref->mtx, mtx_plain);
            {
                fbl::AutoLock lock(&display_ref->mtx);
                display_ref->display_device = this;
            }

            backlight_ops.version = DEVICE_OPS_VERSION;
            backlight_ops.ioctl = backlight_ioctl;
            backlight_ops.release = backlight_release;

            device_add_args_t args = {};
            args.version = DEVICE_ADD_ARGS_VERSION;
            args.name = "backlight";
            args.ctx = display_ref.get();
            args.ops = &backlight_ops;
            args.proto_id = ZX_PROTOCOL_BACKLIGHT;

            if ((status = device_add(controller_->zxdev(), &args, &backlight_device_)) == ZX_OK) {
                display_ref_ = display_ref.release();
            }
        }
        if (display_ref_ == nullptr) {
            LOG_WARN("Failed to add backlight (%d)\n", status);
        }
    }

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
        image_t* image = &primary->image;

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

        auto plane_size = pipe_regs.PlaneSurfaceSize(i).FromValue(0);
        plane_size.set_width_minus_1(plane_width - 1);
        plane_size.set_height_minus_1(plane_height - 1);
        plane_size.WriteTo(mmio_space());

        auto plane_pos = pipe_regs.PlanePosition(i).FromValue(0);
        plane_pos.set_x_pos(primary->dest_frame.x_pos);
        plane_pos.set_y_pos(primary->dest_frame.y_pos);
        plane_pos.WriteTo(mmio_space());

        auto plane_offset = pipe_regs.PlaneOffset(i).FromValue(0);
        plane_offset.set_start_x(x_offset);
        plane_offset.set_start_y(y_offset);
        plane_offset.WriteTo(mmio_space());

        auto stride_reg = pipe_regs.PlaneSurfaceStride(i).FromValue(0);
        stride_reg.set_stride(stride);
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

        auto plane_surface = pipe_regs.PlaneSurface(i).ReadFrom(controller_->mmio_space());
        plane_surface.set_surface_base_addr(base_address >> plane_surface.kRShiftCount);
        plane_surface.WriteTo(controller_->mmio_space());
    }
}

} // namespace i915
