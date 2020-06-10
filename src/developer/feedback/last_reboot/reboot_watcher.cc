// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/last_reboot/reboot_watcher.h"

#include "src/developer/feedback/utils/cobalt/metrics.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {
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
    default:
      return "NOT SUPPORTED";
  }
}

}  // namespace

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
}

}  // namespace feedback
