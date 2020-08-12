// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_watcher.h"

#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace last_reboot {
namespace {

std::string FormatReason(fuchsia::hardware::power::statecontrol::RebootReason reason) {
  using fuchsia::hardware::power::statecontrol::RebootReason;

  switch (reason) {
    case RebootReason::USER_REQUEST:
      return "USER REQUEST";
    case RebootReason::SYSTEM_UPDATE:
      return "SYSTEM UPDATE";
    case RebootReason::HIGH_TEMPERATURE:
      return "HIGH TEMPERATURE";
    case RebootReason::SESSION_FAILURE:
      return "SESSION FAILURE";
    case RebootReason::SYSTEM_FAILURE:
      return "SYSTEM FAILURE";
    default:
      return "NOT SUPPORTED";
  }
}

}  // namespace

ImminentGracefulRebootWatcher::ImminentGracefulRebootWatcher(
    std::shared_ptr<sys::ServiceDirectory> services, const std::string& path,
    cobalt::Logger* cobalt)
    : services_(services), path_(path), cobalt_(cobalt), connection_(this) {
  // TODO(fxbug.dev/52187): Reconnect if the error handler runs.
  connection_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to client of "
                               "fuchsia.hardware.power.statecontrol.RebootMethodsWatcher";
  });
}

void ImminentGracefulRebootWatcher::Connect() {
  // We register ourselves with RebootMethodsWatcher using a fire-and-forget request that gives an
  // endpoint to a long-lived connection we maintain.
  auto reboot_watcher_register =
      services_->Connect<fuchsia::hardware::power::statecontrol::RebootMethodsWatcherRegister>();
  reboot_watcher_register->Register(connection_.NewBinding());
}

void ImminentGracefulRebootWatcher::OnReboot(
    fuchsia::hardware::power::statecontrol::RebootReason reason, OnRebootCallback callback) {
  const std::string content = FormatReason(reason);
  FX_LOGS(INFO) << "Received reboot reason  '" << content << "' ";

  const size_t timer_id = cobalt_->StartTimer();
  if (files::WriteFile(path_, content.c_str(), content.size())) {
    cobalt_->LogElapsedTime(cobalt::RebootReasonWriteResult::kSuccess, timer_id);
  } else {
    cobalt_->LogElapsedTime(cobalt::RebootReasonWriteResult::kFailure, timer_id);
    FX_LOGS(ERROR) << "Failed to write reboot reason '" << FormatReason(reason) << "' to " << path_;
  }

  callback();
  connection_.Unbind();
}

}  // namespace last_reboot
}  // namespace forensics
