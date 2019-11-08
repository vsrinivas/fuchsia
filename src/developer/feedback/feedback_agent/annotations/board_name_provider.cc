// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/feedback_agent/annotations/board_name_provider.h"

#include <fcntl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using fuchsia::feedback::Annotation;

BoardNameProvider::BoardNameProvider() : SingleSyncAnnotationProvider(kAnnotationDeviceBoardName) {}

std::set<std::string> BoardNameProvider::GetSupportedAnnotations() {
  return {
      kAnnotationDeviceBoardName,
  };
}

std::optional<std::string> BoardNameProvider::GetAnnotation() {
  // fuchsia.sysinfo.Device is not Discoverable so we need to construct the channel ourselves.
  const char kSysInfoPath[] = "/dev/misc/sysinfo";
  const int fd = open(kSysInfoPath, O_RDWR);
  if (fd < 0) {
    FX_LOGS(ERROR) << "failed to open " << kSysInfoPath;
    return std::nullopt;
  }

  zx::channel channel;
  const zx_status_t channel_status = fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (channel_status != ZX_OK) {
    FX_PLOGS(ERROR, channel_status) << "failed to open a channel at " << kSysInfoPath;
    return std::nullopt;
  }

  fidl::SynchronousInterfacePtr<fuchsia::sysinfo::Device> device;
  device.Bind(std::move(channel));

  zx_status_t out_status;
  fidl::StringPtr out_board_name;
  const zx_status_t fidl_status = device->GetBoardName(&out_status, &out_board_name);
  if (fidl_status != ZX_OK) {
    FX_PLOGS(ERROR, fidl_status) << "failed to connect to fuchsia.sysinfo.Device";
    return std::nullopt;
  }
  if (out_status != ZX_OK) {
    FX_PLOGS(ERROR, out_status) << "failed to get device board name";
    return std::nullopt;
  }

  if (!out_board_name) {
    FX_PLOGS(ERROR, out_status) << "failed to get device board name";
    return std::nullopt;
  }

  return out_board_name.value();
}

}  // namespace feedback
