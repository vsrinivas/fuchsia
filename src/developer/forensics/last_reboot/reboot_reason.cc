// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_reason.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics {
namespace last_reboot {
namespace {

std::string ToString(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
      return "RebootReason::kNotParseable";
    case RebootReason::kGenericGraceful:
      return "RebootReason::kGenericGraceful";
    case RebootReason::kCold:
      return "RebootReason::kCold";
    case RebootReason::kSpontaneous:
      return "RebootReason::kSpontaneous";
    case RebootReason::kKernelPanic:
      return "RebootReason::kKernelPanic";
    case RebootReason::kOOM:
      return "RebootReason::kOOM";
    case RebootReason::kHardwareWatchdogTimeout:
      return "RebootReason::kHardwareWatchdogTimeout";
    case RebootReason::kSoftwareWatchdogTimeout:
      return "RebootReason::kSoftwareWatchdogTimeout";
    case RebootReason::kBrownout:
      return "RebootReason::kBrownout";
    case RebootReason::kUserRequest:
      return "RebootReason::kUserRequest";
    case RebootReason::kSystemUpdate:
      return "RebootReason::kSystemUpdate";
    case RebootReason::kHighTemperature:
      return "RebootReason::kHighTemperature";
    case RebootReason::kSessionFailure:
      return "RebootReason::kSessionFailure";
    case RebootReason::kSystemFailure:
      return "RebootReason::kSystemFailure";
    case RebootReason::kFdr:
      return "RebootReason::kFdr";
  }
}

}  // namespace

bool IsCrash(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
    case RebootReason::kSpontaneous:
    case RebootReason::kKernelPanic:
    case RebootReason::kOOM:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSoftwareWatchdogTimeout:
    case RebootReason::kBrownout:
    case RebootReason::kSessionFailure:
    case RebootReason::kSystemFailure:
      return true;
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kCold:
    case RebootReason::kFdr:
      return false;
  }
}

std::optional<bool> OptionallyGraceful(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kSessionFailure:
    case RebootReason::kSystemFailure:
    case RebootReason::kFdr:
      return true;
    case RebootReason::kCold:
    case RebootReason::kSpontaneous:
    case RebootReason::kKernelPanic:
    case RebootReason::kOOM:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSoftwareWatchdogTimeout:
    case RebootReason::kBrownout:
      return false;
    case RebootReason::kNotParseable:
      return std::nullopt;
  }
}

cobalt::LastRebootReason ToCobaltLastRebootReason(RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
      return cobalt::LastRebootReason::kUnknown;
    case RebootReason::kGenericGraceful:
      return cobalt::LastRebootReason::kGenericGraceful;
    case RebootReason::kUserRequest:
      return cobalt::LastRebootReason::kUserRequest;
    case RebootReason::kSystemUpdate:
      return cobalt::LastRebootReason::kSystemUpdate;
    case RebootReason::kHighTemperature:
      return cobalt::LastRebootReason::kHighTemperature;
    case RebootReason::kSessionFailure:
      return cobalt::LastRebootReason::kSessionFailure;
    case RebootReason::kSystemFailure:
      return cobalt::LastRebootReason::kSystemFailure;
    case RebootReason::kFdr:
      return cobalt::LastRebootReason::kFactoryDataReset;
    case RebootReason::kCold:
      return cobalt::LastRebootReason::kCold;
    case RebootReason::kSpontaneous:
      return cobalt::LastRebootReason::kBriefPowerLoss;
    case RebootReason::kKernelPanic:
      return cobalt::LastRebootReason::kKernelPanic;
    case RebootReason::kOOM:
      return cobalt::LastRebootReason::kSystemOutOfMemory;
    case RebootReason::kHardwareWatchdogTimeout:
      return cobalt::LastRebootReason::kHardwareWatchdogTimeout;
    case RebootReason::kSoftwareWatchdogTimeout:
      return cobalt::LastRebootReason::kSoftwareWatchdogTimeout;
    case RebootReason::kBrownout:
      return cobalt::LastRebootReason::kBrownout;
  }
}

std::string ToCrashSignature(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
      return "fuchsia-reboot-log-not-parseable";
    case RebootReason::kSpontaneous:
      return "fuchsia-brief-power-loss";
    case RebootReason::kKernelPanic:
      return "fuchsia-kernel-panic";
    case RebootReason::kOOM:
      return "fuchsia-oom";
    case RebootReason::kHardwareWatchdogTimeout:
      return "fuchsia-hw-watchdog-timeout";
    case RebootReason::kSoftwareWatchdogTimeout:
      return "fuchsia-sw-watchdog-timeout";
    case RebootReason::kBrownout:
      return "fuchsia-brownout";
    case RebootReason::kSessionFailure:
      return "fuchsia-session-failure";
    case RebootReason::kSystemFailure:
      return "fuchsia-system-failure";
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kCold:
    case RebootReason::kFdr:
      FX_LOGS(FATAL) << "Not expecting a crash for reboot reason " << ToString(reason);
      return "FATAL ERROR";
  }
}

std::string ToCrashProgramName(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
      return "reboot-log";
    case RebootReason::kKernelPanic:
      return "kernel";
    case RebootReason::kBrownout:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSpontaneous:
      return "device";
    case RebootReason::kOOM:
    case RebootReason::kSoftwareWatchdogTimeout:
    case RebootReason::kSessionFailure:
    case RebootReason::kSystemFailure:
      return "system";
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kCold:
    case RebootReason::kFdr:
      FX_LOGS(FATAL) << "Not expecting a program name request for reboot reason "
                     << ToString(reason);
      return "FATAL ERROR";
  }
}

std::optional<fuchsia::feedback::RebootReason> ToFidlRebootReason(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kGenericGraceful:
      return std::nullopt;
    case RebootReason::kUserRequest:
      return fuchsia::feedback::RebootReason::USER_REQUEST;
    case RebootReason::kSystemUpdate:
      return fuchsia::feedback::RebootReason::SYSTEM_UPDATE;
    case RebootReason::kHighTemperature:
      return fuchsia::feedback::RebootReason::HIGH_TEMPERATURE;
    case RebootReason::kSessionFailure:
      return fuchsia::feedback::RebootReason::SESSION_FAILURE;
    case RebootReason::kSystemFailure:
      return fuchsia::feedback::RebootReason::SYSTEM_FAILURE;
    case RebootReason::kFdr:
      return fuchsia::feedback::RebootReason::FACTORY_DATA_RESET;
    case RebootReason::kCold:
      return fuchsia::feedback::RebootReason::COLD;
    case RebootReason::kSpontaneous:
      return fuchsia::feedback::RebootReason::BRIEF_POWER_LOSS;
    case RebootReason::kKernelPanic:
      return fuchsia::feedback::RebootReason::KERNEL_PANIC;
    case RebootReason::kOOM:
      return fuchsia::feedback::RebootReason::SYSTEM_OUT_OF_MEMORY;
    case RebootReason::kHardwareWatchdogTimeout:
      return fuchsia::feedback::RebootReason::HARDWARE_WATCHDOG_TIMEOUT;
    case RebootReason::kSoftwareWatchdogTimeout:
      return fuchsia::feedback::RebootReason::SOFTWARE_WATCHDOG_TIMEOUT;
    case RebootReason::kBrownout:
      return fuchsia::feedback::RebootReason::BROWNOUT;
    case RebootReason::kNotParseable:
      return std::nullopt;
  }
}

}  // namespace last_reboot
}  // namespace forensics
