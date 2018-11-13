// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "report_annotations.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>

#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/strings/trim.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/channel.h>
#include <zircon/boot/image.h>

namespace fuchsia {
namespace crash {
namespace {

std::string GetBoardName() {
  const char kSysInfoPath[] = "/dev/misc/sysinfo";
  const int fd = open(kSysInfoPath, O_RDWR);
  if (fd < 0) {
    FX_LOGS(ERROR) << "failed to open " << kSysInfoPath;
    return "unknown";
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to get board name";
    return "unknown";
  }

  char board_name[ZBI_BOARD_NAME_LEN];
  size_t actual_size;
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(
      channel.get(), &status, board_name, sizeof(board_name), &actual_size);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to get board name";
    return "unknown";
  }
  return std::string(board_name);
}

std::string GetVersion() {
  const char kFilepath[] = "/config/build-info/last-update";
  std::string build_timestamp;
  if (!files::ReadFileToString(kFilepath, &build_timestamp)) {
    FX_LOGS(ERROR) << "Failed to read build timestamp from '" << kFilepath
                   << "'.";
    return "unknown";
  }
  return fxl::TrimString(build_timestamp, "\r\n").ToString();
}

}  // namespace

std::map<std::string, std::string> MakeAnnotations(
    const std::string& package_name) {
  return {
      {"product", "Fuchsia"},
      {"version", GetVersion()},
      // We use ptype to benefit from Chrome's "Process type" handling in
      // the UI.
      {"ptype", package_name},
      {"board_name", GetBoardName()},
  };
}

}  // namespace crash
}  // namespace fuchsia
