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

DisplayDevice::DisplayDevice(Controller* controller, uint64_t id, registers::Ddi ddi)
        : controller_(controller), id_(id), ddi_(ddi) {}

DisplayDevice::~DisplayDevice() {
    if (pipe_) {
        pipe_->Reset();
    }
    if (inited_) {
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

bool DisplayDevice::ResetDdi() {
    return controller_->ResetDdi(ddi_);
}

bool DisplayDevice::Init() {
    ddi_power_ = controller_->power()->GetDdiPowerWellRef(ddi_);

    if (!QueryDevice(&edid_)) {
        return false;
    }


    auto preferred_timing = edid_.begin();
    if (!(preferred_timing != edid_.end())) {
        return false;
    }

    info_.pixel_clock_10khz = preferred_timing->pixel_freq_10khz;
    info_.h_addressable = preferred_timing->horizontal_addressable;
    info_.h_front_porch = preferred_timing->horizontal_front_porch;
    info_.h_sync_pulse = preferred_timing->horizontal_sync_pulse;
    info_.h_blanking = preferred_timing->horizontal_blanking;
    info_.v_addressable = preferred_timing->vertical_addressable;
    info_.v_front_porch = preferred_timing->vertical_front_porch;
    info_.v_sync_pulse = preferred_timing->vertical_sync_pulse;
    info_.v_blanking = preferred_timing->vertical_blanking;
    info_.flags = preferred_timing->flags;

    if (!ResetDdi() || !ConfigureDdi()) {
        return false;
    }

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
    if (pipe_) {
        pipe_->Resume();
    }
    return true;
}

bool DisplayDevice::AttachPipe(Pipe* pipe) {
    if (pipe == pipe_) {
        return false;
    }

    if (pipe_) {
        pipe_->Reset();
    }
    if (pipe) {
        pipe->AttachToDisplay(id_, controller()->igd_opregion().IsEdp(ddi()));

        PipeConfigPreamble(pipe->pipe(), pipe->transcoder());
        pipe->ApplyModeConfig(info_);
        PipeConfigEpilogue(pipe->pipe(), pipe->transcoder());
    }
    pipe_ = pipe;
    return true;
}

void DisplayDevice::ApplyConfiguration(const display_config_t* config) {
    ZX_ASSERT(config);

    if (memcmp(&config->mode, &info_, sizeof(display_mode_t)) != 0) {
        pipe_->Reset();
        ResetDdi();

        info_ = config->mode;

        ConfigureDdi();

        PipeConfigPreamble(pipe_->pipe(), pipe_->transcoder());
        pipe_->ApplyModeConfig(info_);
        PipeConfigEpilogue(pipe_->pipe(), pipe_->transcoder());
    }

    pipe_->ApplyConfiguration(config);
}

} // namespace i915
