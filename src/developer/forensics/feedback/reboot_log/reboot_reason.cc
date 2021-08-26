// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/reboot_log/reboot_reason.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics {
namespace feedback {

std::string ToString(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
      return "NOT PARSEABLE";
    case RebootReason::kGenericGraceful:
      return "GENERIC GRACEFUL";
    case RebootReason::kCold:
      return "COLD";
    case RebootReason::kSpontaneous:
      return "SPONTANEOUS";
    case RebootReason::kKernelPanic:
      return "KERNEL PANIC";
    case RebootReason::kOOM:
      return "OOM";
    case RebootReason::kHardwareWatchdogTimeout:
      return "HARDWARE WATCHDOG TIMEOUT";
    case RebootReason::kSoftwareWatchdogTimeout:
      return "SOFTWARE WATCHDOG TIMEOUT";
    case RebootReason::kBrownout:
      return "BROWNOUT";
    case RebootReason::kRootJobTermination:
      return "ROOT JOB TERMINATION";
    case RebootReason::kUserRequest:
      return "USER REQUEST";
    case RebootReason::kSystemUpdate:
      return "SYSTEM UPDATE";
    case RebootReason::kRetrySystemUpdate:
      return "RETRY SYSTEM UPDATE";
    case RebootReason::kZbiSwap:
      return "ZBI SWAP";
    case RebootReason::kHighTemperature:
      return "HIGH TEMPERATURE";
    case RebootReason::kSessionFailure:
      return "SESSION FAILURE";
    case RebootReason::kSysmgrFailure:
      return "SYSMGR FAILURE";
    case RebootReason::kCriticalComponentFailure:
      return "CRITICAL COMPONENT FAILURE";
    case RebootReason::kFdr:
      return "FACTORY DATA RESET";
  }
}

bool IsCrash(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
    case RebootReason::kSpontaneous:
    case RebootReason::kKernelPanic:
    case RebootReason::kOOM:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSoftwareWatchdogTimeout:
    case RebootReason::kBrownout:
    case RebootReason::kRootJobTermination:
    case RebootReason::kSessionFailure:
    case RebootReason::kSysmgrFailure:
    case RebootReason::kCriticalComponentFailure:
    case RebootReason::kRetrySystemUpdate:
    case RebootReason::kGenericGraceful:
      return true;
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kZbiSwap:
    case RebootReason::kHighTemperature:
    case RebootReason::kCold:
    case RebootReason::kFdr:
      return false;
  }
}

bool IsFatal(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kNotParseable:
    case RebootReason::kSpontaneous:
    case RebootReason::kKernelPanic:
    case RebootReason::kOOM:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSoftwareWatchdogTimeout:
    case RebootReason::kBrownout:
    case RebootReason::kRootJobTermination:
    case RebootReason::kSysmgrFailure:
    case RebootReason::kCriticalComponentFailure:
    case RebootReason::kRetrySystemUpdate:
    case RebootReason::kGenericGraceful:
      return true;
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kZbiSwap:
    case RebootReason::kHighTemperature:
    case RebootReason::kCold:
    case RebootReason::kSessionFailure:
    case RebootReason::kFdr:
      return false;
  }
}

std::optional<bool> OptionallyGraceful(const RebootReason reason) {
  switch (reason) {
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kRetrySystemUpdate:
    case RebootReason::kZbiSwap:
    case RebootReason::kHighTemperature:
    case RebootReason::kSessionFailure:
    case RebootReason::kSysmgrFailure:
    case RebootReason::kCriticalComponentFailure:
    case RebootReason::kFdr:
      return true;
    case RebootReason::kCold:
    case RebootReason::kSpontaneous:
    case RebootReason::kKernelPanic:
    case RebootReason::kOOM:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSoftwareWatchdogTimeout:
    case RebootReason::kBrownout:
    case RebootReason::kRootJobTermination:
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
    case RebootReason::kRetrySystemUpdate:
      return cobalt::LastRebootReason::kRetrySystemUpdate;
    case RebootReason::kZbiSwap:
      return cobalt::LastRebootReason::kZbiSwap;
    case RebootReason::kHighTemperature:
      return cobalt::LastRebootReason::kHighTemperature;
    case RebootReason::kSessionFailure:
      return cobalt::LastRebootReason::kSessionFailure;
    case RebootReason::kSysmgrFailure:
      return cobalt::LastRebootReason::kSysmgrFailure;
    case RebootReason::kCriticalComponentFailure:
      return cobalt::LastRebootReason::kCriticalComponentFailure;
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
    case RebootReason::kRootJobTermination:
      return cobalt::LastRebootReason::kRootJobTermination;
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
    case RebootReason::kRootJobTermination:
      return "fuchsia-root-job-termination";
    case RebootReason::kSessionFailure:
      return "fuchsia-session-failure";
    case RebootReason::kSysmgrFailure:
      return "fuchsia-sysmgr-failure";
    case RebootReason::kCriticalComponentFailure:
      return "fuchsia-critical-component-failure";
    case RebootReason::kRetrySystemUpdate:
      return "fuchsia-retry-system-update";
    case RebootReason::kGenericGraceful:
      return "fuchsia-undetermined-userspace-reboot";
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kZbiSwap:
    case RebootReason::kHighTemperature:
    case RebootReason::kCold:
    case RebootReason::kFdr:
      FX_LOGS(FATAL) << "Not expecting a crash for reboot reason: " << ToString(reason);
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
    case RebootReason::kRootJobTermination:
    case RebootReason::kSessionFailure:
    case RebootReason::kSysmgrFailure:
    case RebootReason::kCriticalComponentFailure:
    case RebootReason::kRetrySystemUpdate:
    case RebootReason::kGenericGraceful:
      return "system";
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kZbiSwap:
    case RebootReason::kHighTemperature:
    case RebootReason::kCold:
    case RebootReason::kFdr:
      FX_LOGS(FATAL) << "Not expecting a program name request for reboot reason: "
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
    case RebootReason::kRetrySystemUpdate:
      return fuchsia::feedback::RebootReason::RETRY_SYSTEM_UPDATE;
    case RebootReason::kZbiSwap:
      return fuchsia::feedback::RebootReason::ZBI_SWAP;
    case RebootReason::kHighTemperature:
      return fuchsia::feedback::RebootReason::HIGH_TEMPERATURE;
    case RebootReason::kSessionFailure:
      return fuchsia::feedback::RebootReason::SESSION_FAILURE;
    case RebootReason::kSysmgrFailure:
      return fuchsia::feedback::RebootReason::SYSMGR_FAILURE;
    case RebootReason::kCriticalComponentFailure:
      return fuchsia::feedback::RebootReason::CRITICAL_COMPONENT_FAILURE;
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
    case RebootReason::kRootJobTermination:
      return fuchsia::feedback::RebootReason::ROOT_JOB_TERMINATION;
    case RebootReason::kNotParseable:
      return std::nullopt;
  }
}

}  // namespace feedback
}  // namespace forensics
