// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/reboot_log/annotations.h"

#include "fuchsia/feedback/cpp/fidl.h"
#include "src/developer/forensics/utils/time.h"

namespace forensics::feedback {

std::string LastRebootReasonAnnotation(const RebootLog& reboot_log) {
  using FuchsiaRebootReason = fuchsia::feedback::RebootReason;

  // Define a generic value to use in case conversion fails or the converted value fails to match a
  // good value.
  std::string generic_value = "unknown";
  if (const std::optional<bool> graceful_opt = OptionallyGraceful(reboot_log.RebootReason());
      graceful_opt.has_value()) {
    generic_value = (graceful_opt.value()) ? "graceful" : "ungraceful";
  }

  const auto reboot_reason = ToFidlRebootReason(reboot_log.RebootReason());
  if (!reboot_reason) {
    return generic_value;
  }

  switch (reboot_reason.value()) {
    case FuchsiaRebootReason::COLD:
      return "cold";
    case FuchsiaRebootReason::BRIEF_POWER_LOSS:
      return "brief loss of power";
    case FuchsiaRebootReason::BROWNOUT:
      return "brownout";
    case FuchsiaRebootReason::KERNEL_PANIC:
      return "kernel panic";
    case FuchsiaRebootReason::SYSTEM_OUT_OF_MEMORY:
      return "system out of memory";
    case FuchsiaRebootReason::HARDWARE_WATCHDOG_TIMEOUT:
      return "hardware watchdog timeout";
    case FuchsiaRebootReason::SOFTWARE_WATCHDOG_TIMEOUT:
      return "software watchdog timeout";
    case FuchsiaRebootReason::USER_REQUEST:
      return "user request";
    case FuchsiaRebootReason::SYSTEM_UPDATE:
      return "system update";
    case FuchsiaRebootReason::RETRY_SYSTEM_UPDATE:
      return "retry system update";
    case FuchsiaRebootReason::ZBI_SWAP:
      return "ZBI swap";
    case FuchsiaRebootReason::HIGH_TEMPERATURE:
      return "device too hot";
    case FuchsiaRebootReason::SESSION_FAILURE:
      return "fatal session failure";
    case FuchsiaRebootReason::SYSMGR_FAILURE:
      return "fatal sysmgr failure";
    case FuchsiaRebootReason::CRITICAL_COMPONENT_FAILURE:
      return "fatal critical component failure";
    case FuchsiaRebootReason::FACTORY_DATA_RESET:
      return "factory data reset";
    case FuchsiaRebootReason::ROOT_JOB_TERMINATION:
      return "root job termination";
    default:
      return generic_value;
  }
}

ErrorOr<std::string> LastRebootUptimeAnnotation(const RebootLog& reboot_log) {
  if (reboot_log.Uptime().has_value()) {
    const auto uptime = FormatDuration(*reboot_log.Uptime());
    if (uptime.has_value()) {
      return *uptime;
    }
  }

  return Error::kMissingValue;
}

}  // namespace forensics::feedback
