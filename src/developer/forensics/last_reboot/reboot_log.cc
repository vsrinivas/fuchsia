// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_log.h"

#include <lib/syslog/cpp/macros.h>

#include <array>
#include <sstream>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace last_reboot {
namespace {

enum class ZirconRebootReason {
  kNotSet,
  kCold,
  kNoCrash,
  kKernelPanic,
  kOOM,
  kHwWatchdog,
  kSwWatchdog,
  kBrownout,
  kUnknown,
  kNotParseable,
};

enum class GracefulRebootReason {
  kNotSet,
  kNone,
  kUserRequest,
  kSystemUpdate,
  kHighTemperature,
  kSessionFailure,
  kSystemFailure,
  kNotSupported,
  kNotParseable,
};

zx::duration ExtractUptime(const std::string_view line) {
  const std::string line_copy(line);
  return zx::msec(std::stoll(line_copy));
}

ZirconRebootReason ExtractZirconRebootReason(const std::string_view line) {
  if (line == "ZIRCON REBOOT REASON (NO CRASH)") {
    return ZirconRebootReason::kNoCrash;
  } else if (line == "ZIRCON REBOOT REASON (KERNEL PANIC)") {
    return ZirconRebootReason::kKernelPanic;
  } else if (line == "ZIRCON REBOOT REASON (OOM)") {
    return ZirconRebootReason::kOOM;
  } else if (line == "ZIRCON REBOOT REASON (SW WATCHDOG)") {
    return ZirconRebootReason::kSwWatchdog;
  } else if (line == "ZIRCON REBOOT REASON (HW WATCHDOG)") {
    return ZirconRebootReason::kHwWatchdog;
  } else if (line == "ZIRCON REBOOT REASON (BROWNOUT)") {
    return ZirconRebootReason::kBrownout;
  } else if (line == "ZIRCON REBOOT REASON (UNKNOWN)") {
    return ZirconRebootReason::kUnknown;
  }

  FX_LOGS(ERROR) << "Failed to extract a reboot reason from Zircon reboot log";
  return ZirconRebootReason::kNotParseable;
}

GracefulRebootReason ExtractGracefulRebootReason(const std::string_view line) {
  if (line == "USER REQUEST") {
    return GracefulRebootReason::kUserRequest;
  } else if (line == "SYSTEM UPDATE") {
    return GracefulRebootReason::kSystemUpdate;
  } else if (line == "HIGH TEMPERATURE") {
    return GracefulRebootReason::kHighTemperature;
  } else if (line == "SESSION FAILURE") {
    return GracefulRebootReason::kSessionFailure;
  } else if (line == "SYSTEM FAILURE") {
    return GracefulRebootReason::kSystemFailure;
  } else if (line == "NOT SUPPORTED") {
    return GracefulRebootReason::kNotSupported;
  }

  FX_LOGS(ERROR) << "Failed to extract a reboot reason from graceful reboot log";
  return GracefulRebootReason::kNotParseable;
}

void ExtractZirconRebootInfo(const std::string& path, ZirconRebootReason* reason,
                             std::optional<std::string>* content,
                             std::optional<zx::duration>* uptime) {
  if (!files::IsFile(path)) {
    FX_LOGS(INFO) << "No reboot reason found, assuming cold boot";
    *reason = ZirconRebootReason::kCold;
    return;
  }

  std::string file_content;
  if (!files::ReadFileToString(path, &file_content)) {
    FX_LOGS(ERROR) << "Failed to read Zircon reboot log from " << path;
    *reason = ZirconRebootReason::kNotParseable;
    return;
  }

  if (file_content.empty()) {
    FX_LOGS(ERROR) << "Found empty Zircon reboot log at " << path;
    *reason = ZirconRebootReason::kNotParseable;
    return;
  }

  *content = file_content;

  const std::vector<std::string_view> lines =
      fxl::SplitString(content->value(), "\n", fxl::WhiteSpaceHandling::kTrimWhitespace,
                       fxl::SplitResult::kSplitWantNonEmpty);

  if (lines.size() == 0) {
    FX_LOGS(INFO) << "Zircon reboot log has no content";
    return;
  }

  // We expect the format to be:
  //
  // ZIRCON REBOOT REASON (<SOME REASON>)
  // <empty>
  // UPTIME (ms)
  // <SOME UPTIME>
  *reason = ExtractZirconRebootReason(lines[0]);

  if (lines.size() < 3) {
    FX_LOGS(ERROR) << "Zircon reboot log is missing uptime information";
    return;
  }

  if (lines[1] != "UPTIME (ms)") {
    FX_LOGS(ERROR) << "'UPTIME(ms)' not present, found '" << lines[1] << "'";
    return;
  }

  *uptime = ExtractUptime(lines[2]);
}

void ExtractGracefulRebootInfo(const std::string& path, GracefulRebootReason* reason,
                               std::optional<std::string>* content) {
  if (!files::IsFile(path)) {
    *reason = GracefulRebootReason::kNone;
    return;
  }

  std::string file_content;
  if (!files::ReadFileToString(path, &file_content)) {
    FX_LOGS(ERROR) << "Failed to read graceful reboot log from " << path;
    *reason = GracefulRebootReason::kNotParseable;
    return;
  }

  if (file_content.empty()) {
    FX_LOGS(ERROR) << "Found empty graceful reboot log at " << path;
    *reason = GracefulRebootReason::kNotParseable;
    return;
  }

  *content = file_content;

  *reason = ExtractGracefulRebootReason(content->value());
}

RebootReason DetermineGracefulRebootReason(GracefulRebootReason graceful_reason) {
  switch (graceful_reason) {
    case GracefulRebootReason::kUserRequest:
      return RebootReason::kUserRequest;
    case GracefulRebootReason::kSystemUpdate:
      return RebootReason::kSystemUpdate;
    case GracefulRebootReason::kHighTemperature:
      return RebootReason::kHighTemperature;
    case GracefulRebootReason::kSessionFailure:
      return RebootReason::kSessionFailure;
    case GracefulRebootReason::kSystemFailure:
      return RebootReason::kSystemFailure;
    case GracefulRebootReason::kNotSupported:
    case GracefulRebootReason::kNone:
    case GracefulRebootReason::kNotParseable:
      return RebootReason::kGenericGraceful;
    case GracefulRebootReason::kNotSet:
      FX_LOGS(FATAL) << "Graceful reboot reason must be set";
      return RebootReason::kNotParseable;
  }
}

RebootReason DetermineRebootReason(ZirconRebootReason zircon_reason,
                                   GracefulRebootReason graceful_reason) {
  switch (zircon_reason) {
    case ZirconRebootReason::kCold:
      return RebootReason::kCold;
    case ZirconRebootReason::kKernelPanic:
      return RebootReason::kKernelPanic;
    case ZirconRebootReason::kOOM:
      return RebootReason::kOOM;
    case ZirconRebootReason::kHwWatchdog:
      return RebootReason::kHardwareWatchdogTimeout;
    case ZirconRebootReason::kSwWatchdog:
      return RebootReason::kSoftwareWatchdogTimeout;
    case ZirconRebootReason::kBrownout:
      return RebootReason::kBrownout;
    case ZirconRebootReason::kUnknown:
      return RebootReason::kSpontaneous;
    case ZirconRebootReason::kNotParseable:
      return RebootReason::kNotParseable;
    case ZirconRebootReason::kNoCrash:
      return DetermineGracefulRebootReason(graceful_reason);
    case ZirconRebootReason::kNotSet:
      FX_LOGS(FATAL) << "|zircon_reason| must be set";
      return RebootReason::kNotParseable;
  }
}

std::optional<std::string> MakeRebootLog(const std::optional<std::string>& zircon_reboot_log,
                                         const std::optional<std::string>& graceful_reboot_log) {
  std::vector<std::string> lines;

  if (zircon_reboot_log.has_value()) {
    lines.push_back(zircon_reboot_log.value());
  }

  if (graceful_reboot_log.has_value()) {
    lines.push_back(
        fxl::StringPrintf("GRACEFUL REBOOT REASON (%s)", graceful_reboot_log.value().c_str()));
  }

  if (lines.empty()) {
    return std::nullopt;
  } else {
    return fxl::JoinStrings(lines, "\n");
  }
}

}  // namespace

// static
RebootLog RebootLog::ParseRebootLog(const std::string& zircon_reboot_log_path,
                                    const std::string& graceful_reboot_log_path) {
  ZirconRebootReason zircon_reason = ZirconRebootReason::kNotSet;
  std::optional<std::string> zircon_reboot_log;
  std::optional<zx::duration> last_boot_uptime;
  ExtractZirconRebootInfo(zircon_reboot_log_path, &zircon_reason, &zircon_reboot_log,
                          &last_boot_uptime);

  GracefulRebootReason graceful_reason = GracefulRebootReason::kNotSet;
  std::optional<std::string> graceful_reboot_log;
  ExtractGracefulRebootInfo(graceful_reboot_log_path, &graceful_reason, &graceful_reboot_log);

  const auto reboot_reason = DetermineRebootReason(zircon_reason, graceful_reason);
  const auto reboot_log = MakeRebootLog(zircon_reboot_log, graceful_reboot_log);

  if (reboot_log.has_value()) {
    FX_LOGS(INFO) << "Found reboot log:\n" << reboot_log.value();
  }

  return RebootLog(reboot_reason, reboot_log, last_boot_uptime);
}

RebootLog::RebootLog(enum RebootReason reboot_reason, std::optional<std::string> reboot_log_str,
                     std::optional<zx::duration> last_boot_uptime)
    : reboot_reason_(reboot_reason),
      reboot_log_str_(reboot_log_str),
      last_boot_uptime_(last_boot_uptime) {}

}  // namespace last_reboot
}  // namespace forensics
