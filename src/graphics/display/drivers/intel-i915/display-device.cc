// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display-device.h"

#include <float.h>
#include <lib/zx/vmo.h>
#include <math.h>

#include <ddktl/fidl.h>

#include "intel-i915.h"
#include "macros.h"
#include "registers-dpll.h"
#include "registers-transcoder.h"
#include "registers.h"
#include "tiling.h"

namespace {

zx_status_t backlight_message(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  i915::DisplayDevice* ptr;
  {
    fbl::AutoLock lock(&static_cast<i915::display_ref_t*>(ctx)->mtx);
    ptr = static_cast<i915::display_ref_t*>(ctx)->display_device;
  }
  llcpp::fuchsia::hardware::backlight::Device::Dispatch(ptr, msg, &transaction);
  return transaction.Status();
}

void backlight_release(void* ctx) { delete static_cast<i915::display_ref_t*>(ctx); }

static zx_protocol_device_t backlight_ops = {};

}  // namespace

namespace i915 {

DisplayDevice::DisplayDevice(Controller* controller, uint64_t id, registers::Ddi ddi)
    : controller_(controller), id_(id), ddi_(ddi) {}

DisplayDevice::~DisplayDevice() {
  if (pipe_) {
    pipe_->Reset();
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

ddk::MmioBuffer* DisplayDevice::mmio_space() const { return controller_->mmio_space(); }

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

      backlight_ops.version = DEVICE_OPS_VERSION;
      backlight_ops.message = backlight_message;
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
      zxlogf(WARNING, "Failed to add backlight (%d)", status);
    }

    SetBacklightState(true, 1.0);
  }
}

bool DisplayDevice::Resume() {
  if (!DdiModeset(info_, pipe_->pipe(), pipe_->transcoder())) {
    return false;
  }
  if (pipe_) {
    pipe_->Resume();
  }
  return true;
}

void DisplayDevice::LoadActiveMode() {
  pipe_->LoadActiveMode(&info_);
  info_.pixel_clock_10khz = LoadClockRateForTranscoder(pipe_->transcoder());
}

bool DisplayDevice::AttachPipe(Pipe* pipe) {
  if (pipe == pipe_) {
    return false;
  }

  if (pipe_) {
    pipe_->Reset();
    pipe_->Detach();
  }
  if (pipe) {
    pipe->AttachToDisplay(id_, controller()->igd_opregion().IsEdp(ddi()));

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
  if (memcmp(&mode->h_addressable, &info_.h_addressable, cmp_end - cmp_start)) {
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
  auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(mmio_space());
  const dpll_state_t* current_state = nullptr;
  if (!dpll_ctrl2.ddi_clock_off(ddi()).get()) {
    current_state = controller_->GetDpllState(
        static_cast<registers::Dpll>(dpll_ctrl2.ddi_clock_select(ddi()).get()));
  }

  if (current_state == nullptr) {
    zxlogf(DEBUG, "Modeset necessary for clock");
    return true;
  }

  dpll_state_t new_state;
  if (!ComputeDpllState(mode->pixel_clock_10khz, &new_state)) {
    // ComputeDpllState should be validated in the display's CheckDisplayMode
    ZX_ASSERT(false);
  }

  // Modesetting is necessary if the states are not equal
  bool res = !Controller::CompareDpllStates(*current_state, new_state);
  if (res) {
    zxlogf(DEBUG, "Modeset necessary for clock state");
  }
  return res;
}

void DisplayDevice::ApplyConfiguration(const display_config_t* config) {
  ZX_ASSERT(config);

  if (CheckNeedsModeset(&config->mode)) {
    info_ = config->mode;

    DdiModeset(info_, pipe_->pipe(), pipe_->transcoder());

    PipeConfigPreamble(info_, pipe_->pipe(), pipe_->transcoder());
    pipe_->ApplyModeConfig(info_);
    PipeConfigEpilogue(info_, pipe_->pipe(), pipe_->transcoder());
  }

  pipe_->ApplyConfiguration(config);
}

void DisplayDevice::GetStateNormalized(GetStateNormalizedCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;
  FidlBacklight::State state = {};

  if (display_ref_ != nullptr) {
    fbl::AutoLock lock(&display_ref_->mtx);
    if (display_ref_->display_device != nullptr) {
      status =
          display_ref_->display_device->GetBacklightState(&state.backlight_on, &state.brightness);
    }
  } else {
    status = ZX_ERR_BAD_STATE;
  }

  FidlBacklight::Device_GetStateNormalized_Result result;
  FidlBacklight::Device_GetStateNormalized_Response response{.state = state};
  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }
  completer.Reply(std::move(result));
}

void DisplayDevice::SetStateNormalized(FidlBacklight::State state,
                                       SetStateNormalizedCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;

  if (display_ref_ != nullptr) {
    fbl::AutoLock lock(&display_ref_->mtx);
    if (display_ref_->display_device != nullptr) {
      status =
          display_ref_->display_device->SetBacklightState(state.backlight_on, state.brightness);
    }
  } else {
    status = ZX_ERR_BAD_STATE;
  }

  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void DisplayDevice::GetStateAbsolute(GetStateAbsoluteCompleter::Sync& completer) {
  FidlBacklight::Device_GetStateAbsolute_Result result;
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  result.set_err(fidl::unowned_ptr(&status));
  completer.Reply(std::move(result));
}

void DisplayDevice::SetStateAbsolute(FidlBacklight::State state,
                                     SetStateAbsoluteCompleter::Sync& completer) {
  FidlBacklight::Device_SetStateAbsolute_Result result;
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DisplayDevice::GetMaxAbsoluteBrightness(GetMaxAbsoluteBrightnessCompleter::Sync& completer) {
  FidlBacklight::Device_GetMaxAbsoluteBrightness_Result result;
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DisplayDevice::SetNormalizedBrightnessScale(
    __UNUSED double scale, SetNormalizedBrightnessScaleCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DisplayDevice::GetNormalizedBrightnessScale(
    GetNormalizedBrightnessScaleCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace i915
