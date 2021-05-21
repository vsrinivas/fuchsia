// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_GRACEFUL_REBOOT_REASON_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_GRACEFUL_REBOOT_REASON_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>

#include <string>

namespace forensics {
namespace feedback {

// Feedback's internal representation of why a device rebooted gracefully.
//
// These values should not be used to understand why a device has rebooted outside of this
// component.
enum class GracefulRebootReason {
  kNotSet,
  kNone,
  kUserRequest,
  kSystemUpdate,
  kRetrySystemUpdate,
  kHighTemperature,
  kSessionFailure,
  kSysmgrFailure,
  kCriticalComponentFailure,
  kFdr,
  kZbiSwap,
  kNotSupported,
  kNotParseable,
};

std::string ToString(GracefulRebootReason reason);

GracefulRebootReason ToGracefulRebootReason(
    fuchsia::hardware::power::statecontrol::RebootReason reason);

// The input is limited to values corresponding to |power::statecontrol::RebootReason|.
GracefulRebootReason FromFileContent(std::string content);

// The input is limited to values corresponding to |power::statecontrol::RebootReason|.
std::string ToFileContent(GracefulRebootReason reason);

}  // namespace feedback
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_REBOOT_LOG_GRACEFUL_REBOOT_REASON_H_
