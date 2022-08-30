// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/linux_runner/linux_runner.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/virtualization/bin/linux_runner/block_devices.h"

namespace linux_runner {

constexpr std::string_view kLinuxEnvironmentName("termina");
constexpr size_t kStatefulImageSize = 40ul * 1024 * 1024 * 1024;  // 40 GB
constexpr size_t kBytesToWipe = 1ul * 1024 * 1024;                // 1 MiB

LinuxRunner::LinuxRunner(async_dispatcher_t* dispatcher)
    : LinuxRunner(dispatcher, sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

LinuxRunner::LinuxRunner(async_dispatcher_t* dispatcher,
                         std::unique_ptr<sys::ComponentContext> context)
    : GuestManager(dispatcher, context.get()), context_(std::move(context)) {
  context_->outgoing()->AddPublicService(manager_bindings_.GetHandler(this));
}

zx_status_t LinuxRunner::Init() {
  TRACE_DURATION("linux_runner", "LinuxRunner::Init");
  GuestConfig config{
      .env_label = kLinuxEnvironmentName,
      .stateful_image_size = kStatefulImageSize,
  };
  return Guest::CreateAndStart(
      context_.get(), config, *this,
      [this](GuestInfo info) { OnGuestInfoChanged(std::move(info)); }, &guest_);
}

void LinuxRunner::StartAndGetLinuxGuestInfo(std::string label,
                                            StartAndGetLinuxGuestInfoCallback callback) {
  TRACE_DURATION("linux_runner", "LinuxRunner::StartAndGetLinuxGuestInfo");

  if (guest_ == nullptr) {
    zx_status_t status = Init();
    if (status != ZX_OK) {
      callback(fpromise::error(ZX_ERR_INTERNAL));
    }
  }

  // Linux runner is currently limited to a single environment name.
  if (label != kLinuxEnvironmentName) {
    FX_LOGS(ERROR) << "Invalid Linux environment: " << label;
    callback(fpromise::error(ZX_ERR_UNAVAILABLE));
    return;
  }

  // If the container startup failed, we can request a retry.
  if (info_ && info_->container_status == fuchsia::virtualization::ContainerStatus::FAILED) {
    info_ = std::nullopt;
    guest_->RetryContainerStartup();
  }

  if (info_.has_value()) {
    fuchsia::virtualization::LinuxGuestInfo info;
    info.set_cid(info_->cid);
    info.set_container_status(info_->container_status);
    info.set_download_percent(info_->download_percent);
    info.set_failure_reason(info_->failure_reason);
    fuchsia::virtualization::LinuxManager_StartAndGetLinuxGuestInfo_Response response;
    response.info = std::move(info);
    callback(fuchsia::virtualization::LinuxManager_StartAndGetLinuxGuestInfo_Result::WithResponse(
        std::move(response)));
  } else {
    callbacks_.push_back(std::move(callback));
  }
}

void LinuxRunner::WipeData(WipeDataCallback callback) {
  if (guest_) {
    callback(fuchsia::virtualization::LinuxManager_WipeData_Result::WithErr(ZX_ERR_BAD_STATE));
    return;
  }
  // We zero out some bytes at the beginning of the partition to corrupt any filesystem data-
  // structures stored there.
  zx::status<> status = WipeStatefulPartition(kBytesToWipe);
  if (status.is_error()) {
    callback(fuchsia::virtualization::LinuxManager_WipeData_Result::WithErr(status.status_value()));
  } else {
    callback(fuchsia::virtualization::LinuxManager_WipeData_Result::WithResponse(
        fuchsia::virtualization::LinuxManager_WipeData_Response()));
  }
}

void LinuxRunner::OnGuestInfoChanged(GuestInfo info) {
  info_ = info;
  while (!callbacks_.empty()) {
    fuchsia::virtualization::LinuxGuestInfo info;
    info.set_cid(info_->cid);
    info.set_container_status(fuchsia::virtualization::ContainerStatus::TRANSIENT);
    fuchsia::virtualization::LinuxManager_StartAndGetLinuxGuestInfo_Response response;
    response.info = std::move(info);
    callbacks_.front()(
        fuchsia::virtualization::LinuxManager_StartAndGetLinuxGuestInfo_Result::WithResponse(
            std::move(response)));
    callbacks_.pop_front();
  }
  for (auto& binding : manager_bindings_.bindings()) {
    fuchsia::virtualization::LinuxGuestInfo info;
    info.set_cid(info_->cid);
    info.set_container_status(info_->container_status);
    info.set_download_percent(info_->download_percent);
    info.set_failure_reason(info_->failure_reason);
    binding->events().OnGuestInfoChanged(std::string(kLinuxEnvironmentName), std::move(info));
  }
}

}  // namespace linux_runner
