// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_log.h"

#include <lib/syslog/cpp/macros.h>

#include <array>
#include <sstream>

#include "src/developer/forensics/last_reboot/graceful_reboot_reason.h"
#include "src/developer/forensics/last_reboot/reboot_reason.h"
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
  kRootJobTermination,
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
  } else if (line == "ZIRCON REBOOT REASON (USERSPACE ROOT JOB TERMINATION)") {
    return ZirconRebootReason::kRootJobTermination;
  }

  FX_LOGS(ERROR) << "Failed to extract a reboot reason from Zircon reboot log";
  return ZirconRebootReason::kNotParseable;
}

ZirconRebootReason ExtractZirconRebootInfo(const std::string& path,
                                           std::optional<std::string>* content,
                                           std::optional<zx::duration>* uptime) {
  if (!files::IsFile(path)) {
    return ZirconRebootReason::kCold;
  }

  std::string file_content;
  if (!files::ReadFileToString(path, &file_content)) {
    FX_LOGS(ERROR) << "Failed to read Zircon reboot log from " << path;
    return ZirconRebootReason::kNotParseable;
  }

  if (file_content.empty()) {
    FX_LOGS(ERROR) << "Found empty Zircon reboot log at " << path;
    return ZirconRebootReason::kNotParseable;
  }

  *content = file_content;

  const std::vector<std::string_view> lines =
      fxl::SplitString(content->value(), "\n", fxl::WhiteSpaceHandling::kTrimWhitespace,
                       fxl::SplitResult::kSplitWantNonEmpty);

  if (lines.size() == 0) {
    FX_LOGS(ERROR) << "Zircon reboot log has no content";
    return ZirconRebootReason::kNotSet;
  }

  // We expect the format to be:
  //
  // ZIRCON REBOOT REASON (<SOME REASON>)
  // <empty>
  // UPTIME (ms)
  // <SOME UPTIME>
  const auto reason = ExtractZirconRebootReason(lines[0]);

  if (lines.size() < 3) {
    FX_LOGS(ERROR) << "Zircon reboot log is missing uptime information";
  } else if (lines[1] != "UPTIME (ms)") {
    FX_LOGS(ERROR) << "'UPTIME(ms)' not present, found '" << lines[1] << "'";
  } else {
    *uptime = ExtractUptime(lines[2]);
  }

  return reason;
}

GracefulRebootReason ExtractGracefulRebootInfo(const std::string& graceful_reboot_log_path) {
  if (!files::IsFile(graceful_reboot_log_path)) {
    return GracefulRebootReason::kNone;
  }

  std::string file_content;
  if (!files::ReadFileToString(graceful_reboot_log_path, &file_content)) {
    return GracefulRebootReason::kNotParseable;
  }

  if (file_content.empty()) {
    return GracefulRebootReason::kNotParseable;
  }

  return FromFileContent(file_content);
}

RebootReason DetermineRebootReason(const ZirconRebootReason zircon_reason,
                                   const GracefulRebootReason graceful_reason,
                                   const std::string& not_a_fdr_path) {
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
    case ZirconRebootReason::kRootJobTermination:
      return RebootReason::kRootJobTermination;
    case ZirconRebootReason::kNotParseable:
      return RebootReason::kNotParseable;
    case ZirconRebootReason::kNoCrash:
      if (!files::IsFile(not_a_fdr_path)) {
        return RebootReason::kFdr;
      }
      switch (graceful_reason) {
        case GracefulRebootReason::kUserRequest:
          return RebootReason::kUserRequest;
        case GracefulRebootReason::kSystemUpdate:
          return RebootReason::kSystemUpdate;
        case GracefulRebootReason::kRetrySystemUpdate:
          return RebootReason::kRetrySystemUpdate;
        case GracefulRebootReason::kHighTemperature:
          return RebootReason::kHighTemperature;
        case GracefulRebootReason::kSessionFailure:
          return RebootReason::kSessionFailure;
        case GracefulRebootReason::kSysmgrFailure:
          return RebootReason::kSysmgrFailure;
        case GracefulRebootReason::kCriticalComponentFailure:
          return RebootReason::kCriticalComponentFailure;
        case GracefulRebootReason::kFdr:
          return RebootReason::kFdr;
        case GracefulRebootReason::kZbiSwap:
          return RebootReason::kZbiSwap;
        case GracefulRebootReason::kNotSupported:
        case GracefulRebootReason::kNone:
        case GracefulRebootReason::kNotParseable:
          return RebootReason::kGenericGraceful;
        case GracefulRebootReason::kNotSet:
          FX_LOGS(FATAL) << "Graceful reboot reason must be set";
          return RebootReason::kNotParseable;
      }
    case ZirconRebootReason::kNotSet:
      FX_LOGS(FATAL) << "|zircon_reason| must be set";
      return RebootReason::kNotParseable;
  }
}

std::string MakeRebootLog(const std::optional<std::string>& zircon_reboot_log,
                          const GracefulRebootReason graceful_reason,
                          const RebootReason reboot_reason) {
  std::vector<std::string> lines;

  if (zircon_reboot_log.has_value()) {
    lines.push_back(zircon_reboot_log.value());
  }

  lines.push_back(
      fxl::StringPrintf("GRACEFUL REBOOT REASON (%s)\n", ToString(graceful_reason).c_str()));

  lines.push_back(fxl::StringPrintf("FINAL REBOOT REASON (%s)", ToString(reboot_reason).c_str()));

  return fxl::JoinStrings(lines, "\n");
}

}  // namespace

// static
RebootLog RebootLog::ParseRebootLog(const std::string& zircon_reboot_log_path,
                                    const std::string& graceful_reboot_log_path,
                                    const std::string& not_a_fdr_path) {
  std::optional<std::string> zircon_reboot_log;
  std::optional<zx::duration> last_boot_uptime;
  const auto zircon_reason =
      ExtractZirconRebootInfo(zircon_reboot_log_path, &zircon_reboot_log, &last_boot_uptime);

  const auto graceful_reason = ExtractGracefulRebootInfo(graceful_reboot_log_path);

  const auto reboot_reason = DetermineRebootReason(zircon_reason, graceful_reason, not_a_fdr_path);
  const auto reboot_log = MakeRebootLog(zircon_reboot_log, graceful_reason, reboot_reason);

  FX_LOGS(INFO) << "Reboot info:\n" << reboot_log;

  return RebootLog(reboot_reason, reboot_log, last_boot_uptime);
}

RebootLog::RebootLog(enum RebootReason reboot_reason, std::string reboot_log_str,
                     std::optional<zx::duration> last_boot_uptime)
    : reboot_reason_(reboot_reason),
      reboot_log_str_(reboot_log_str),
      last_boot_uptime_(last_boot_uptime) {}

}  // namespace last_reboot
}  // namespace forensics
