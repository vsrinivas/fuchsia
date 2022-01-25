// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/linux_runner/linux_runner.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace linux_runner {

constexpr std::string_view kLinuxEnvironmentName("termina");
constexpr size_t kStatefulImageSize = 40ul * 1024 * 1024 * 1024;  // 40 GB

LinuxRunner::LinuxRunner() : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
  context_->outgoing()->AddPublicService(manager_bindings_.GetHandler(this));
}

zx_status_t LinuxRunner::Init() {
  TRACE_DURATION("linux_runner", "LinuxRunner::Init");
  GuestConfig config{
      .env_label = kLinuxEnvironmentName,
      .stateful_image_size = kStatefulImageSize,
  };
  return Guest::CreateAndStart(
      context_.get(), config, [this](GuestInfo info) { OnGuestInfoChanged(std::move(info)); },
      &guest_);
}

void LinuxRunner::StartAndGetLinuxGuestInfo(std::string label,
                                            StartAndGetLinuxGuestInfoCallback callback) {
  TRACE_DURATION("linux_runner", "LinuxRunner::StartAndGetLinuxGuestInfo");

  // Linux runner is currently limited to a single environment name.
  if (label != kLinuxEnvironmentName) {
    FX_LOGS(ERROR) << "Invalid Linux environment: " << label;
    callback(fuchsia::virtualization::LinuxManager_StartAndGetLinuxGuestInfo_Result::WithErr(
        ZX_ERR_UNAVAILABLE));
    return;
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
