// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/vmm_controller.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

namespace vmm {

using ::fuchsia::virtualization::GuestConfig;
using ::fuchsia::virtualization::GuestError;
using ::fuchsia::virtualization::GuestLifecycle;

VmmController::VmmController(fit::function<void()> stop_component_callback,
                             std::unique_ptr<sys::ComponentContext> context,
                             async_dispatcher_t* dispatcher)
    : stop_component_callback_(std::move(stop_component_callback)),
      context_(std::move(context)),
      dispatcher_(dispatcher) {
  FX_CHECK(stop_component_callback_);
  FX_CHECK(dispatcher_ != nullptr);

  bindings_.set_empty_set_handler([this]() { LifecycleChannelClosed(); });
  FX_CHECK(context_->outgoing()->AddPublicService(bindings_.GetHandler(this)) == ZX_OK);
}

void VmmController::Create(GuestConfig guest_config, CreateCallback callback) {
  if (run_callback_.has_value()) {
    callback(fpromise::error(GuestError::ALREADY_RUNNING));
    return;
  }

  if (vmm_) {
    vmm_.reset();
  }

  auto vmm = std::make_unique<vmm::Vmm>();
  fitx::result<GuestError> result =
      vmm->Initialize(std::move(guest_config), context_.get(), dispatcher_);
  if (!result.is_ok()) {
    callback(fpromise::error(result.error_value()));
    return;
  }

  vmm_ = std::move(vmm);
  callback(fpromise::ok());
}

void VmmController::Run(RunCallback callback) {
  if (!vmm_) {
    callback(fpromise::error(GuestError::NOT_CREATED));
    return;
  }

  if (run_callback_.has_value()) {
    callback(fpromise::error(GuestError::ALREADY_RUNNING));
    return;
  }

  auto result = vmm_->StartPrimaryVcpu(
      [this](fitx::result<GuestError> result) { ScheduleVmmTeardown(result); });
  if (result.is_error()) {
    vmm_.reset();
    callback(fpromise::error(result.error_value()));
    return;
  }

  run_callback_ = std::move(callback);
}

void VmmController::Stop(StopCallback callback) {
  ScheduleVmmTeardown(fitx::error(GuestError::CONTROLLER_FORCED_HALT));
  callback();
}

void VmmController::LifecycleChannelClosed() {
  FX_LOGS(INFO) << "A client closed the lifecycle channel, shutting down the VMM component";
  stop_component_callback_();
}

void VmmController::ScheduleVmmTeardown(fitx::result<GuestError> result) {
  const zx_status_t status =
      async::PostTask(dispatcher_, [this, result]() { DestroyAndRespond(result); });

  // If ZX_OK, the task was successfully scheduled. If ZX_ERR_BAD_STATE, the component is already
  // shutting down so there is nothing to do.
  if (status != ZX_OK && status != ZX_ERR_BAD_STATE) {
    FX_LOGS(WARNING) << "Failed to schedule a VMM teardown, so shutting down the component instead";
    stop_component_callback_();
  }
}

void VmmController::DestroyAndRespond(fitx::result<::fuchsia::virtualization::GuestError> result) {
  if (vmm_) {
    vmm_->NotifyClientsShutdown(result.is_ok() ? ZX_OK : ZX_ERR_INTERNAL);
  }

  vmm_.reset();

  if (run_callback_.has_value()) {
    RunCallback callback = std::exchange(run_callback_, std::nullopt).value();
    if (result.is_error()) {
      callback(fpromise::error(result.error_value()));
    } else {
      callback(fpromise::ok());
    }
  }
}

}  // namespace vmm
