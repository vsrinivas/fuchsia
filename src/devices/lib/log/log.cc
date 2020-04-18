// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log.h"

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>

zx_status_t log_to_debuglog() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect("/svc/fuchsia.boot.WriteOnlyLog", remote.release());
  if (status != ZX_OK) {
    return status;
  }
  llcpp::fuchsia::boot::WriteOnlyLog::SyncClient write_only_log(std::move(local));
  auto result = write_only_log.Get();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  char process_name[ZX_MAX_NAME_LEN] = {};
  zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  const char* tag = process_name;
  fx_logger_config_t logger_config{
      .min_severity = FX_LOG_INFO,
      .console_fd = -1,
      .log_service_channel = ZX_HANDLE_INVALID,
      .tags = &tag,
      .num_tags = 1,
  };
  status = fdio_fd_create(result.Unwrap()->log.release(), &logger_config.console_fd);
  if (status != ZX_OK) {
    return status;
  }
  return fx_log_reconfigure(&logger_config);
}
