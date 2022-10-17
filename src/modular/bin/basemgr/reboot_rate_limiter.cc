// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/reboot_rate_limiter.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cerrno>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include <src/lib/files/file.h>
#include <src/lib/files/path.h>
#include <src/lib/fxl/strings/split_string.h>
#include <src/lib/fxl/strings/string_number_conversions.h>

namespace modular {

namespace {

// Maps the global |errno| variable to Zircon status. This is not intended to
// be an exhaustive list, but rather a reasonal approximation of all the POSIX
// errors we can expect to encounter in the functions below.
zx_status_t errno_to_status() {
  switch (errno) {
    case EBADF:
      return ZX_ERR_BAD_HANDLE;
    case EINVAL:
      return ZX_ERR_INVALID_ARGS;
    case ENOTDIR:
      return ZX_ERR_NOT_DIR;
    case EACCES:
      return ZX_ERR_ACCESS_DENIED;
    case ENOENT:
      return ZX_ERR_NOT_FOUND;
    default:
      return ZX_ERR_INTERNAL;
  };
}
}  // namespace

RebootRateLimiter::RebootRateLimiter(std::string tracking_file_path, size_t backoff_base,
                                     size_t max_delay, Duration tracking_file_ttl)
    : tracking_file_path_(std::move(tracking_file_path)),
      backoff_base_(backoff_base),
      max_delay_(max_delay),
      tracking_file_ttl_(tracking_file_ttl) {}

zx::result<bool> RebootRateLimiter::CanReboot(TimePoint timepoint) {
  // File is empty, assume that this is first attempt. It's safe to reboot.
  if (!files::IsFile(tracking_file_path_)) {
    FX_LOGS(INFO) << "File doesn't exit at path: " << tracking_file_path_.data();
    return zx::ok(true);
  }

  std::string content;
  if (!files::ReadFileToString(tracking_file_path_, &content)) {
    zx_status_t status = errno_to_status();
    FX_LOGS(ERROR) << "Failed to read file " << tracking_file_path_.data()
                   << " even though it exists: " << zx_status_get_string(status);
    return zx::error(status);
  }

  auto status = DeserializeLastReboot(content);
  if (status.is_error()) {
    FX_LOGS(ERROR)
        << "Failed to parse tracking file. Will return true in order to overwrite corrupted data.";
    return status.take_error();
  }

  auto [last_reboot_timestamp, reboot_counter] = status.value();

  Duration elapsed_time_since_last_reboot = timepoint - last_reboot_timestamp;
  if (elapsed_time_since_last_reboot > tracking_file_ttl_) {
    if (!files::DeletePath(tracking_file_path_, /*recursive=*/false)) {
      zx_status_t status = errno_to_status();
      FX_LOGS(INFO) << "Failed to delete tracking file path " << tracking_file_path_
                    << " after TTL expired: " << zx_status_get_string(status);
      return zx::error(status);
    }

    return zx::ok(true);
  }

  Duration backoff_threshold = std::chrono::minutes(static_cast<size_t>(
      std::pow(static_cast<double>(backoff_base_), static_cast<double>(reboot_counter))));

  return zx::ok(elapsed_time_since_last_reboot > backoff_threshold ||
                backoff_threshold >= std::chrono::minutes(max_delay_));
}

zx::result<> RebootRateLimiter::UpdateTrackingFile(TimePoint timepoint) {
  // Default to 0 in case no file is present.
  size_t reboot_counter = 0;

  std::string content;
  if (files::ReadFileToString(tracking_file_path_, &content)) {
    auto status = DeserializeLastReboot(content);
    if (status.is_ok()) {
      reboot_counter = status.value().second;
    } else {
      FX_LOGS(ERROR) << "Failed to parse tracking file " << tracking_file_path_ << ": "
                     << zx_status_get_string(status.error_value());
    }
  }

  content = SerializeLastReboot(timepoint, reboot_counter + 1);
  if (!files::WriteFile(tracking_file_path_, content)) {
    zx_status_t status = errno_to_status();
    FX_LOGS(ERROR) << "Failed to update tracking file: " << zx_status_get_string(status);
    return zx::error(status);
  }

  return zx::ok();
}

std::string RebootRateLimiter::SerializeLastReboot(TimePoint timepoint, size_t reboots) {
  std::time_t timepoint_as_time_t = SystemClock::to_time_t(timepoint);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&timepoint_as_time_t), kTimestampFormat) << std::endl
     << fxl::NumberToString(reboots);

  return ss.str();
}

zx::result<std::pair<RebootRateLimiter::TimePoint, size_t>>
RebootRateLimiter::DeserializeLastReboot(std::string_view payload) {
  auto parts = fxl::SplitString(payload, "\n", fxl::WhiteSpaceHandling::kTrimWhitespace,
                                fxl::SplitResult::kSplitWantAll);
  if (parts.size() != 2) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  // First line stores the timestamp of the last reboot.
  auto last_reboot_timestamp_str = parts[0].data();
  std::istringstream ss(last_reboot_timestamp_str);
  std::tm timestamp;
  ss >> std::get_time(&timestamp, kTimestampFormat);
  if (ss.fail() || ss.bad() || ss.eof()) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  TimePoint last_reboot_timestamp = SystemClock::from_time_t(std::mktime(&timestamp));

  // Second line stores the reboot counter.
  auto reboot_counter_str = parts[1];
  size_t reboot_counter;
  if (!fxl::StringToNumberWithError(reboot_counter_str, &reboot_counter)) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  return zx::ok(std::make_pair(last_reboot_timestamp, reboot_counter));
}
}  // namespace modular
