// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback_agent/annotations.h"

#include <fcntl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/fit/promise.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace fuchsia {
namespace feedback {
namespace {

fit::promise<std::string> GetDeviceBoardName() {
  // fuchsia.sysinfo.Device is not Discoverable so we need to construct the channel ourselves.
  const char kSysInfoPath[] = "/dev/misc/sysinfo";
  const int fd = open(kSysInfoPath, O_RDWR);
  if (fd < 0) {
    FX_LOGS(ERROR) << "failed to open " << kSysInfoPath;
    return fit::make_result_promise<std::string>(fit::error());
  }

  zx::channel channel;
  const zx_status_t channel_status = fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (channel_status != ZX_OK) {
    FX_PLOGS(ERROR, channel_status) << "failed to open a channel at " << kSysInfoPath;
    return fit::make_result_promise<std::string>(fit::error());
  }

  fidl::SynchronousInterfacePtr<fuchsia::sysinfo::Device> device;
  device.Bind(std::move(channel));

  zx_status_t out_status;
  fidl::StringPtr out_board_name;
  const zx_status_t fidl_status = device->GetBoardName(&out_status, &out_board_name);
  if (fidl_status != ZX_OK) {
    FX_PLOGS(ERROR, fidl_status) << "failed to connect to fuchsia.sysinfo.Device";
    return fit::make_result_promise<std::string>(fit::error());
  }
  if (out_status != ZX_OK) {
    FX_PLOGS(ERROR, out_status) << "failed to get device board name";
    return fit::make_result_promise<std::string>(fit::error());
  }
  return fit::make_ok_promise(std::string(out_board_name));
}

fit::promise<std::string> ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "failed to read content from " << filepath;
    return fit::make_result_promise<std::string>(fit::error());
  }
  return fit::make_ok_promise(fxl::TrimString(content, "\r\n").ToString());
}

fit::promise<std::string> BuildValue(const std::string& key) {
  if (key == "device.board-name") {
    return GetDeviceBoardName();
  } else if (key == "build.board") {
    return ReadStringFromFile("/config/build-info/board");
  } else if (key == "build.product") {
    return ReadStringFromFile("/config/build-info/product");
  } else if (key == "build.latest-commit-date") {
    return ReadStringFromFile("/config/build-info/latest-commit-date");
  } else if (key == "build.version") {
    return ReadStringFromFile("/config/build-info/version");
  } else {
    FX_LOGS(WARNING) << "Unknown annotation " << key;
    return fit::make_result_promise<std::string>(fit::error());
  }
}

fit::promise<Annotation> BuildAnnotation(const std::string& key) {
  return BuildValue(key)
      .and_then([key](std::string& value) -> fit::result<Annotation> {
        Annotation annotation;
        annotation.key = key;
        annotation.value = value;
        return fit::ok(std::move(annotation));
      })
      .or_else([key]() {
        FX_LOGS(WARNING) << "Failed to build annotation " << key;
        return fit::error();
      });
}

}  // namespace

std::vector<fit::promise<Annotation>> GetAnnotations(const std::set<std::string>& allowlist) {
  if (allowlist.empty()) {
    FX_LOGS(WARNING) << "Annotation allowlist is empty, nothing to retrieve";
    return {};
  }

  std::vector<fit::promise<Annotation>> annotations;
  for (const auto& key : allowlist) {
    annotations.push_back(BuildAnnotation(key));
  }
  return annotations;
}

}  // namespace feedback
}  // namespace fuchsia
