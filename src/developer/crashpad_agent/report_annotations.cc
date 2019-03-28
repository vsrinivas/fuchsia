// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/report_annotations.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fxl/strings/trim.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/channel.h>
#include <zircon/boot/image.h>

#include "src/lib/files/file.h"

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
  size_t actual_size = 0;
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetBoardName(
      channel.get(), &status, board_name, sizeof(board_name), &actual_size);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to get board name";
    return "unknown";
  }
  return std::string(board_name, actual_size);
}

std::string ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from '" << filepath << "'.";
    return "unknown";
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

}  // namespace

std::map<std::string, std::string> MakeDefaultAnnotations(
    const std::string& package_name) {
  const std::string version = ReadStringFromFile("/config/build-info/version");
  return {
      {"product", "Fuchsia"},
      {"version", version},
      // We use ptype to benefit from Chrome's "Process type" handling in
      // the UI.
      {"ptype", package_name},
      {"board_name", GetBoardName()},
      {"build.product", ReadStringFromFile("/config/build-info/product")},
      {"build.board", ReadStringFromFile("/config/build-info/board")},
      {"build.version", version},
      {"build.latest-commit-date",
       ReadStringFromFile("/config/build-info/latest-commit-date")},
  };
}

std::map<std::string, std::string> MakeManagedRuntimeExceptionAnnotations(
    ManagedRuntimeLanguage language, const std::string& component_url,
    const std::string& exception) {
  std::map<std::string, std::string> annotations =
      MakeDefaultAnnotations(component_url);
  if (language == ManagedRuntimeLanguage::DART) {
    annotations["type"] = "DartError";
    // In the Dart C++ runner, the runtime type has already been pre-pended to
    // the error message so we expect the format to be '$RuntimeType: $Message'.
    const size_t delimiter_pos = exception.find_first_of(':');
    if (delimiter_pos == std::string::npos) {
      FX_LOGS(ERROR) << "error parsing Dart exception: expected format "
                        "'$RuntimeType: $Message', got '"
                     << exception << "'";
      // We still need to specify a type, otherwise the stack trace does not
      // show up in the crash server UI.
      annotations["error_runtime_type"] = "UnknownError";
      annotations["error_message"] = exception;
    } else {
      annotations["error_runtime_type"] = exception.substr(0, delimiter_pos);
      annotations["error_message"] = exception.substr(
          delimiter_pos + 2 /*to get rid of the leading ': '*/);
    }
  } else {
    annotations["error_message"] = exception;
  }
  return annotations;
}

}  // namespace crash
}  // namespace fuchsia
