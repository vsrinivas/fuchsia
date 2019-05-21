// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/annotations.h"

#include <fcntl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace fuchsia {
namespace feedback {
namespace {

Annotation BuildAnnotation(const std::string& key, const std::string& value) {
  Annotation annotation;
  annotation.key = key;
  annotation.value = value;
  return annotation;
}

std::optional<std::string> GetDeviceBoardName() {
  // fuchsia.sysinfo.Device is not Discoverable so we need to construct the
  // channel ourselves.
  const char kSysInfoPath[] = "/dev/misc/sysinfo";
  const int fd = open(kSysInfoPath, O_RDWR);
  if (fd < 0) {
    FX_LOGS(ERROR) << "failed to open " << kSysInfoPath;
    return std::nullopt;
  }

  zx::channel channel;
  const zx_status_t channel_status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (channel_status != ZX_OK) {
    FX_PLOGS(ERROR, channel_status)
        << "failed to open a channel at " << kSysInfoPath;
    return std::nullopt;
  }

  fidl::SynchronousInterfacePtr<fuchsia::sysinfo::Device> device;
  device.Bind(std::move(channel));

  zx_status_t out_status;
  fidl::StringPtr out_board_name;
  const zx_status_t fidl_status =
      device->GetBoardName(&out_status, &out_board_name);
  if (fidl_status != ZX_OK) {
    FX_PLOGS(ERROR, fidl_status)
        << "failed to connect to fuchsia.sysinfo.Device";
    return std::nullopt;
  }
  if (out_status != ZX_OK) {
    FX_PLOGS(ERROR, out_status) << "failed to get device board name";
    return std::nullopt;
  }
  return out_board_name;
}

std::optional<std::string> ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "failed to read content from " << filepath;
    return std::nullopt;
  }
  return fxl::TrimString(content, "\r\n").ToString();
}

void PushBackIfValuePresent(const std::string& key,
                            const std::optional<std::string> value,
                            std::vector<Annotation>* annotations) {
  if (value.has_value()) {
    annotations->push_back(BuildAnnotation(key, value.value()));
  } else {
    FX_LOGS(WARNING) << "missing annotation " << key;
  }
}

}  // namespace

std::vector<Annotation> GetAnnotations() {
  std::vector<Annotation> annotations;
  PushBackIfValuePresent("device.board-name", GetDeviceBoardName(),
                         &annotations);
  PushBackIfValuePresent("build.board",
                         ReadStringFromFile("/config/build-info/board"),
                         &annotations);
  PushBackIfValuePresent("build.product",
                         ReadStringFromFile("/config/build-info/product"),
                         &annotations);
  PushBackIfValuePresent(
      "build.latest-commit-date",
      ReadStringFromFile("/config/build-info/latest-commit-date"),
      &annotations);
  PushBackIfValuePresent("build.version",
                         ReadStringFromFile("/config/build-info/version"),
                         &annotations);
  return annotations;
}

}  // namespace feedback
}  // namespace fuchsia
