// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/bringup/bin/console-launcher/autorun.h"
#include "src/bringup/bin/console-launcher/console_launcher.h"
#include "src/bringup/bin/console-launcher/virtcon-setup.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

#define LOGF(severity, message...) FX_LOGF(severity, nullptr, message)

int main(int argv, char** argc) {
  if (zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
    FX_PLOGS(ERROR, status)
        << "Failed to redirect stdout to debuglog, assuming test environment and continuing";
  }

  // Anything before the log_to_debuglog check should go through stdout, so if there are errors
  // they will make it to the debuglog.
  printf("console-launcher: running\n");

  zx::status boot_args = service::Connect<fuchsia_boot::Arguments>();
  if (boot_args.is_error()) {
    fprintf(stderr, "console-launcher: failed to get boot args: %s\n", boot_args.status_string());
    return 1;
  }

  zx::status args = console_launcher::GetArguments(boot_args.value());
  if (args.is_error()) {
    fprintf(stderr, "console-launcher: Failed to get arguments: %s`\n", args.status_string());
    return 1;
  }

  zx::status log_client_end = service::Connect<fuchsia_boot::WriteOnlyLog>();
  if (log_client_end.is_error()) {
    fprintf(stderr, "console-launcher: failed to get write log: %s\n",
            log_client_end.status_string());
    return 1;
  }
  fidl::WireSyncClient log_client = fidl::BindSyncClient(std::move(log_client_end).value());

  // Past this point we should be using logging instead of stdout.
  if (args->log_to_debuglog) {
    fidl::WireResult result = log_client->Get();
    if (!result.ok()) {
      fprintf(stderr, "Failed to get write-only log: %s\n", result.FormatDescription().c_str());
      return 1;
    }
    char process_name[ZX_MAX_NAME_LEN] = {};
    if (zx_status_t status =
            zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
        status != ZX_OK) {
      fprintf(stderr, "Failed to get process name: %s\n", zx_status_get_string(status));
      return 1;
    }
    const char* tag = process_name;
    fx_logger_config_t logger_config = {
        .min_severity = FX_LOG_SEVERITY_DEFAULT,
        .console_fd = -1,
        .log_sink_channel = ZX_HANDLE_INVALID,
        .log_sink_socket = ZX_HANDLE_INVALID,
        .log_service_channel = ZX_HANDLE_INVALID,
        .tags = &tag,
        .num_tags = 1,
    };
    if (zx_status_t status =
            fdio_fd_create(result.value().log.release(), &logger_config.console_fd);
        status != ZX_OK) {
      fprintf(stderr, "Failed to create log fd: %s\n", zx_status_get_string(status));
      return 1;
    }
    if (zx_status_t status = fx_log_reconfigure(&logger_config); status != ZX_OK) {
      fprintf(stderr, "Failed to configure syslog: %s\n", zx_status_get_string(status));
      return 1;
    }
  }

  if (zx_status_t status = console_launcher::SetupVirtcon(boot_args.value()); status != ZX_OK) {
    // If launching virtcon fails, we still should continue so that the autorun programs
    // and serial console are launched.
    LOGF(ERROR, "Failed to start virtcon shells: %s", zx_status_get_string(status));
  }

  if (!args->run_shell) {
    if (!args->autorun_boot.empty()) {
      LOGF(ERROR, "Couldn't launch autorun command '%s'", args->autorun_boot.c_str());
    }
    return 0;
  }

  zx::status<console_launcher::ConsoleLauncher> result =
      console_launcher::ConsoleLauncher::Create();
  if (!result.is_ok()) {
    LOGF(ERROR, "Failed to create ConsoleLauncher: %s", result.status_string());
    return 1;
  }
  auto& launcher = result.value();
  LOGF(INFO, "console.shell: enabled");

  autorun::AutoRun autorun;
  if (!args->autorun_boot.empty()) {
    fidl::WireResult result = log_client->Get();
    if (!result.ok()) {
      LOGF(ERROR, "console-launcher: failed to get debuglog '%s'", result.status_string());
      return 1;
    }
    if (zx_status_t status = autorun.SetupBootCmd(args->autorun_boot, launcher.shell_job(),
                                                  std::move(result.value().log));
        status != ZX_OK) {
      LOGF(ERROR, "Autorun: Failed to setup boot command: %s", zx_status_get_string(status));
    }
  }
  if (!args->autorun_system.empty()) {
    fidl::WireResult result = log_client->Get();
    if (!result.ok()) {
      LOGF(ERROR, "console-launcher: failed to get debuglog '%s'", result.status_string());
      return 1;
    }
    if (zx_status_t status = autorun.SetupSystemCmd(args->autorun_system, launcher.shell_job(),
                                                    std::move(result.value().log));
        status != ZX_OK) {
      LOGF(ERROR, "Autorun: Failed to setup system command: %s", zx_status_get_string(status));
    }
  }

  while (true) {
    if (zx_status_t status = launcher.LaunchShell(*args); status != ZX_OK) {
      LOGF(ERROR, "console-launcher: failed to launch shell '%s'", zx_status_get_string(status));
      return 1;
    }

    if (zx_status_t status = launcher.WaitForShellExit(); status != ZX_OK) {
      LOGF(ERROR, "console-launcher: failed to wait for shell exit: '%s'",
           zx_status_get_string(status));
      return 1;
    }
  }
}
