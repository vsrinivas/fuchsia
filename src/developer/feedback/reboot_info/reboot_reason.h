// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_REBOOT_REASON_H_
#define SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_REBOOT_REASON_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <optional>
#include <string>

#include "src/developer/feedback/utils/cobalt/metrics.h"

namespace feedback {

// Feedback's internal representation of why a device rebooted.
//
// These values should not be used to understand why a device has rebooted outside of this
// component.
enum class RebootReason {
  // Default value to encode when the reboot reason hasn't been set.
  kNotSet,
  // We could not make a reboot reason out of the reboot log.
  kNotParseable,
  kGenericGraceful,
  kCold,
  // The device spontaneously rebooted, e.g., brief loss of power.
  kSpontaneous,
  kKernelPanic,
  kOOM,
  kHardwareWatchdogTimeout,
  kSoftwareWatchdogTimeout,
  kBrownout,
};

bool IsGraceful(RebootReason reboot_reason);
cobalt::RebootReason ToCobaltRebootReason(RebootReason reboot_reason);
std::string ToCrashSignature(RebootReason reboot_reason);
std::string ToCrashProgramName(RebootReason reboot_reason);
std::optional<fuchsia::feedback::RebootReason> ToFidlRebootReason(RebootReason reboot_reason);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_REBOOT_REASON_H_
