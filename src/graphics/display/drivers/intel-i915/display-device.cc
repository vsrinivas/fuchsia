// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/display-device.h"

#include <float.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fit/function.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <math.h>
#include <zircon/errors.h>

#include <ddktl/fidl.h>

#include "src/graphics/display/drivers/intel-i915/intel-i915.h"
#include "src/graphics/display/drivers/intel-i915/macros.h"
#include "src/graphics/display/drivers/intel-i915/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915/registers-transcoder.h"
#include "src/graphics/display/drivers/intel-i915/registers.h"
#include "src/graphics/display/drivers/intel-i915/tiling.h"

namespace {

zx_status_t backlight_message(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  i915::DisplayDevice* ptr;
  {
    fbl::AutoLock lock(&static_cast<i915::display_ref_t*>(ctx)->mtx);
    ptr = static_cast<i915::display_ref_t*>(ctx)->display_device;
  }
  fidl::WireDispatch<fuchsia_hardware_backlight::Device>(
      ptr, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg), &transaction);
  return transaction.Status();
}

void backlight_release(void* ctx) { delete static_cast<i915::display_ref_t*>(ctx); }

constexpr zx_protocol_device_t kBacklightDeviceOps = {
    .version = DEVICE_OPS_VERSION,
    .release = &backlight_release,
    .message = &backlight_message,
};

}  // namespace

namespace i915 {

DisplayDevice::DisplayDevice(Controller* controller, uint64_t id, registers::Ddi ddi, Type type)
    : controller_(controller), id_(id), ddi_(ddi), type_(type) {}

DisplayDevice::~DisplayDevice() {
  if (pipe_) {
    pipe_->Reset(controller_);
    pipe_->Detach();
  }
  if (inited_) {
    controller_->ResetDdi(ddi());
  }
  if (display_ref_) {
    fbl::AutoLock lock(&display_ref_->mtx);
    display_ref_->display_device = nullptr;
    device_async_remove(backlight_device_);
  }
}

fdf::MmioBuffer* DisplayDevice::mmio_space() const { return controller_->mmio_space(); }

bool DisplayDevice::Init() {
  ddi_power_ = controller_->power()->GetDdiPowerWellRef(ddi_);

  if (!InitDdi()) {
    return false;
  }

  inited_ = true;

  InitBacklight();

  return true;
}

void DisplayDevice::InitBacklight() {
  if (HasBacklight() && InitBacklightHw()) {
    fbl::AllocChecker ac;
    auto display_ref = fbl::make_unique_checked<display_ref_t>(&ac);
    zx_status_t status = ZX_ERR_NO_MEMORY;
    if (ac.check()) {
      mtx_init(&display_ref->mtx, mtx_plain);
      {
        fbl::AutoLock lock(&display_ref->mtx);
        display_ref->display_device = this;
      }

      device_add_args_t args = {
          .version = DEVICE_ADD_ARGS_VERSION,
          .name = "backlight",
          .ctx = display_ref.get(),
          .ops = &kBacklightDeviceOps,
          .proto_id = ZX_PROTOCOL_BACKLIGHT,
      };

      status = device_add(controller_->zxdev(), &args, &backlight_device_);
      if (status == ZX_OK) {
        display_ref_ = display_ref.release();
      }
    }
    if (display_ref_ == nullptr) {
      zxlogf(WARNING, "Failed to add backlight (%d)", status);
    }

    std::ignore = SetBacklightState(true, 1.0);
  }
}

bool DisplayDevice::Resume() {
  if (pipe_) {
    if (!DdiModeset(info_, pipe_->pipe(), pipe_->transcoder())) {
      return false;
    }
    controller_->interrupts()->EnablePipeVsync(pipe_->pipe(), true);
  }
  return pipe_ != nullptr;
}

void DisplayDevice::LoadActiveMode() {
  pipe_->LoadActiveMode(&info_);
  info_.pixel_clock_10khz = LoadClockRateForTranscoder(pipe_->transcoder());
  zxlogf(INFO, "Active pixel clock: %u0 kHz", info_.pixel_clock_10khz);
}

bool DisplayDevice::AttachPipe(Pipe* pipe) {
  if (pipe == pipe_) {
    return false;
  }

  if (pipe_) {
    pipe_->Reset(controller_);
    pipe_->Detach();
  }
  if (pipe) {
    pipe->AttachToDisplay(id_, type() == Type::kEdp);

    if (info_.h_addressable) {
      PipeConfigPreamble(info_, pipe->pipe(), pipe->transcoder());
      pipe->ApplyModeConfig(info_);
      PipeConfigEpilogue(info_, pipe->pipe(), pipe->transcoder());
    }
  }
  pipe_ = pipe;
  return true;
}

bool DisplayDevice::CheckNeedsModeset(const display_mode_t* mode) {
  // Check the clock and the flags later
  size_t cmp_start = offsetof(display_mode_t, h_addressable);
  size_t cmp_end = offsetof(display_mode_t, flags);
  if (memcmp(&mode->h_addressable, &info_.h_addressable, cmp_end - cmp_start) != 0) {
    // Modeset is necessary if display params other than the clock frequency differ
    zxlogf(DEBUG, "Modeset necessary for display params");
    return true;
  }

  // TODO(stevensd): There are still some situations where the BIOS is better at setting up
  // the display than we are. The BIOS seems to not always set the hsync/vsync polarity, so
  // don't include that in the check for already initialized displays. Once we're better at
  // initializing displays, merge the flags check back into the above memcmp.
  if ((mode->flags & MODE_FLAG_INTERLACED) != (info_.flags & MODE_FLAG_INTERLACED)) {
    zxlogf(DEBUG, "Modeset necessary for display flags");
    return true;
  }

  if (mode->pixel_clock_10khz == info_.pixel_clock_10khz) {
    // Modeset is necessary not necessary if all display params are the same
    return false;
  }

  // Check to see if the hardware was already configured properly. The is primarily to
  // prevent unnecessary modesetting at startup. The extra work this adds to regular
  // modesetting is negligible.
  DpllState new_state;
  if (!ComputeDpllState(mode->pixel_clock_10khz, &new_state)) {
    // ComputeDpllState should be validated in the display's CheckDisplayMode
    ZX_ASSERT(false);
  }

  return controller()->dpll_manager()->PllNeedsReset(ddi(), new_state);
}

void DisplayDevice::ApplyConfiguration(const display_config_t* config,
                                       const config_stamp_t* config_stamp) {
  ZX_ASSERT(config);

  if (CheckNeedsModeset(&config->mode)) {
    info_ = config->mode;

    if (pipe_) {
      DdiModeset(info_, pipe_->pipe(), pipe_->transcoder());

      PipeConfigPreamble(info_, pipe_->pipe(), pipe_->transcoder());
      pipe_->ApplyModeConfig(info_);
      PipeConfigEpilogue(info_, pipe_->pipe(), pipe_->transcoder());
    }
  }

  if (pipe_) {
    pipe_->ApplyConfiguration(
        config, config_stamp,
        [controller = controller_](const image_t* image, uint32_t rotation) -> const GttRegion& {
          return controller->SetupGttImage(image, rotation);
        });
  }
}

void DisplayDevice::GetStateNormalized(GetStateNormalizedCompleter::Sync& completer) {
  zx::result<FidlBacklight::wire::State> backlight_state = zx::error(ZX_ERR_BAD_STATE);

  if (display_ref_ != nullptr) {
    fbl::AutoLock lock(&display_ref_->mtx);
    if (display_ref_->display_device != nullptr) {
      backlight_state = display_ref_->display_device->GetBacklightState();
    }
  }

  if (backlight_state.is_ok()) {
    completer.ReplySuccess(backlight_state.value());
  } else {
    completer.ReplyError(backlight_state.status_value());
  }
}

void DisplayDevice::SetStateNormalized(SetStateNormalizedRequestView request,
                                       SetStateNormalizedCompleter::Sync& completer) {
  zx::result<> status = zx::error(ZX_ERR_BAD_STATE);

  if (display_ref_ != nullptr) {
    fbl::AutoLock lock(&display_ref_->mtx);
    if (display_ref_->display_device != nullptr) {
      status = display_ref_->display_device->SetBacklightState(request->state.backlight_on,
                                                               request->state.brightness);
    }
  }

  if (status.is_error()) {
    completer.ReplyError(status.status_value());
    return;
  }
  completer.ReplySuccess();
}

void DisplayDevice::GetStateAbsolute(GetStateAbsoluteCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DisplayDevice::SetStateAbsolute(SetStateAbsoluteRequestView request,
                                     SetStateAbsoluteCompleter::Sync& completer) {
  FidlBacklight::wire::DeviceSetStateAbsoluteResult result;
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DisplayDevice::GetMaxAbsoluteBrightness(GetMaxAbsoluteBrightnessCompleter::Sync& completer) {
  FidlBacklight::wire::DeviceGetMaxAbsoluteBrightnessResult result;
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DisplayDevice::SetNormalizedBrightnessScale(
    SetNormalizedBrightnessScaleRequestView request,
    SetNormalizedBrightnessScaleCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DisplayDevice::GetNormalizedBrightnessScale(
    GetNormalizedBrightnessScaleCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace i915
