// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/reboot_info/reboot_log.h"

#include <lib/syslog/cpp/macros.h>

#include <array>
#include <sstream>

#include "src/lib/files/file.h"

namespace feedback {
namespace {

RebootReason ExtractRebootReason(const std::string& line) {
  if (line == "ZIRCON REBOOT REASON (NO CRASH)") {
    return RebootReason::kGenericGraceful;
  } else if (line == "ZIRCON REBOOT REASON (KERNEL PANIC)") {
    return RebootReason::kKernelPanic;
  } else if (line == "ZIRCON REBOOT REASON (OOM)") {
    return RebootReason::kOOM;
  } else if (line == "ZIRCON REBOOT REASON (SW WATCHDOG)") {
    return RebootReason::kSoftwareWatchdogTimeout;
  } else if (line == "ZIRCON REBOOT REASON (HW WATCHDOG)") {
    return RebootReason::kHardwareWatchdogTimeout;
  } else if (line == "ZIRCON REBOOT REASON (BROWNOUT)") {
    return RebootReason::kBrownout;
  } else if (line == "ZIRCON REBOOT REASON (UNKNOWN)") {
    return RebootReason::kSpontaneous;
  }

  FX_LOGS(ERROR) << "Failed to extract a reboot reason from first line of reboot log";
  return RebootReason::kNotParseable;
}

zx::duration ExtractUptime(const std::string& line) { return zx::msec(std::stoll(line)); }

void ExtractRebootInfo(const std::string& path, enum RebootReason* reboot_reason,
                       std::optional<std::string>* reboot_log_str,
                       std::optional<zx::duration>* last_boot_uptime) {
  // We first check for the existence of the reboot log and attempt to parse it.
  if (!files::IsFile(path)) {
    FX_LOGS(INFO) << "No reboot reason found, assuming cold boot";
    *reboot_reason = RebootReason::kCold;
    return;
  }

  std::string reboot_log_contents;
  if (!files::ReadFileToString(path, &reboot_log_contents)) {
    FX_LOGS(ERROR) << "Failed to read reboot log from " << path;
    *reboot_reason = RebootReason::kNotParseable;
    return;
  }

  if (reboot_log_contents.empty()) {
    FX_LOGS(ERROR) << "Found empty reboot log at " << path;
    *reboot_reason = RebootReason::kNotParseable;
    return;
  }

  *reboot_log_str = reboot_log_contents;
  FX_LOGS(INFO) << "Found reboot log:\n" << reboot_log_str->value();

  std::istringstream iss(reboot_log_str->value());
  std::string line;

  // We expect the format to be:
  //
  // ZIRCON REBOOT REASON (<SOME REASON>)
  // <empty>
  // UPTIME (ms)
  // <SOME UPTIME>
  if (!std::getline(iss, line)) {
    FX_LOGS(ERROR) << "Failed to read first line of reboot log";
    return;
  }
  *reboot_reason = ExtractRebootReason(line);

  if (!std::getline(iss, line)) {
    FX_LOGS(ERROR) << "Failed to read second line of reboot log";
    return;
  }
  if (!line.empty()) {
    FX_LOGS(ERROR) << "Expected second line of reboot log to be empty, found '" << line << "'";
    return;
  }

  if (!std::getline(iss, line)) {
    FX_LOGS(ERROR) << "Failed to read third line of reboot log";
    return;
  }
  if (line != "UPTIME (ms)") {
    FX_LOGS(ERROR) << "Unexpected third line '" << line << "'";
    return;
  }

  if (!std::getline(iss, line)) {
    FX_LOGS(ERROR) << "Failed to read fourth line of reboot log";
    return;
  }
  *last_boot_uptime = ExtractUptime(line);
}

}  // namespace

RebootLog RebootLog::ParseRebootLog(const std::string& path) {
  enum RebootReason reboot_reason = RebootReason::kNotSet;
  std::optional<std::string> reboot_log_str;
  std::optional<zx::duration> last_boot_uptime;

  ExtractRebootInfo(path, &reboot_reason, &reboot_log_str, &last_boot_uptime);

  return RebootLog(reboot_reason, reboot_log_str, last_boot_uptime);
}

RebootLog::RebootLog(enum RebootReason reboot_reason, std::optional<std::string> reboot_log_str,
                     std::optional<zx::duration> last_boot_uptime)
    : reboot_reason_(reboot_reason),
      reboot_log_str_(reboot_log_str),
      last_boot_uptime_(last_boot_uptime) {
  FX_CHECK(reboot_reason != RebootReason::kNotSet) << "Reboot reason must be set";
}

}  // namespace feedback
