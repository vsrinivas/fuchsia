// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/utils/use_debug_log.h"

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <zircon/processargs.h>

#include <iostream>

#include "lib/syslog/cpp/log_settings.h"
#include "lib/syslog/cpp/logging_backend.h"

namespace storage {
namespace {

zx_status_t LogToDebugLog(fuchsia_boot::WriteOnlyLog::SyncClient log_client) {
  auto result = log_client.Get();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  char process_name[ZX_MAX_NAME_LEN] = {};
  zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  const char* tag = process_name;
  fx_logger_config_t logger_config{
      .min_severity = fx_logger_get_min_severity(fx_log_get_logger()),
      .console_fd = -1,
      .log_service_channel = ZX_HANDLE_INVALID,
      .tags = &tag,
      .num_tags = 1,
  };
  zx_status_t status = fdio_fd_create(result.Unwrap()->log.release(), &logger_config.console_fd);
  if (status != ZX_OK) {
    return status;
  }
  syslog::LogSettings settings;
  settings.log_fd = logger_config.console_fd;
  syslog_backend::SetLogSettings(settings);
  return fx_log_reconfigure(&logger_config);
}

zx::status<fuchsia_boot::WriteOnlyLog::SyncClient> ConnectToWriteLog() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = fdio_service_connect("/svc/fuchsia.boot.WriteOnlyLog", remote.release());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(fuchsia_boot::WriteOnlyLog::SyncClient(std::move(local)));
}

}  // namespace

void UseDebugLog(const std::string& tag) {
  auto log_client_or = ConnectToWriteLog();
  if (log_client_or.is_error()) {
    std::cerr << tag
              << ": failed to get write log: " << zx_status_get_string(log_client_or.error_value())
              << std::endl;
    return;
  }

  if (auto status = LogToDebugLog(std::move(log_client_or.value())); status != ZX_OK) {
    std::cerr << tag
              << ": Failed to reconfigure logger to use debuglog: " << zx_status_get_string(status)
              << std::endl;
    return;
  }
}
}  // namespace storage
