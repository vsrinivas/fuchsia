// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/bringup/bin/console-launcher/console_launcher.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace {

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
      .min_severity = fx_logger_get_min_severity(fx_log_get_logger()),
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

}  // namespace

int main(int argv, char** argc) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    FX_LOGS(ERROR)
        << "Failed to redirect stdout to debuglog, assuming test environment and continuing";
  }
  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return 1;
  }
  status = fdio_service_connect("/svc/fuchsia.boot.Arguments", remote.release());
  if (status != ZX_OK) {
    return 1;
  }

  llcpp::fuchsia::boot::Arguments::SyncClient client(std::move(local));
  std::optional<console_launcher::Arguments> args = console_launcher::GetArguments(&client);
  if (!args) {
    FX_LOGS(ERROR) << "console-launcher: Failed to get arguments";
    return 1;
  }

  if (args->log_to_debuglog) {
    zx_status_t status = log_to_debuglog();
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "Failed to reconfigure logger to use debuglog: %s",
              zx_status_get_string(status));
      return status;
    }
  }

  console_launcher::ConsoleLauncher launcher;
  while (true) {
    status = launcher.LaunchShell(*args);
    if (status != ZX_OK) {
      return 1;
    }

    status = launcher.WaitForShellExit();
    if (status != ZX_OK) {
      return 1;
    }
  }
}
