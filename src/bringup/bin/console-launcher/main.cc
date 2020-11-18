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

#include "src/bringup/bin/console-launcher/autorun.h"
#include "src/bringup/bin/console-launcher/console_launcher.h"
#include "src/bringup/bin/console-launcher/virtcon-setup.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace {

#define LOGF(severity, message...) FX_LOGF(severity, nullptr, message)

zx_status_t log_to_debuglog(llcpp::fuchsia::boot::WriteOnlyLog::SyncClient* log_client) {
  auto result = log_client->Get();
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
  return fx_log_reconfigure(&logger_config);
}

zx_status_t ConnectToBootArgs(llcpp::fuchsia::boot::Arguments::SyncClient* out_client) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect("/svc/fuchsia.boot.Arguments", remote.release());
  if (status != ZX_OK) {
    return status;
  }

  *out_client = llcpp::fuchsia::boot::Arguments::SyncClient(std::move(local));
  return ZX_OK;
}

zx_status_t ConnectToWriteLog(llcpp::fuchsia::boot::WriteOnlyLog::SyncClient* out_client) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect("/svc/fuchsia.boot.WriteOnlyLog", remote.release());
  if (status != ZX_OK) {
    return status;
  }

  *out_client = llcpp::fuchsia::boot::WriteOnlyLog::SyncClient(std::move(local));
  return ZX_OK;
}

}  // namespace

int main(int argv, char** argc) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    FX_LOGS(ERROR)
        << "Failed to redirect stdout to debuglog, assuming test environment and continuing";
  }

  // Anything before the log_to_debuglog check should go through stdout, so if there are errors
  // they will make it to the debuglog.
  printf("console-launcher: running\n");

  llcpp::fuchsia::boot::Arguments::SyncClient boot_args;
  status = ConnectToBootArgs(&boot_args);
  if (status != ZX_OK) {
    fprintf(stderr, "console-launcher: failed to get boot args: %s\n",
            zx_status_get_string(status));
    return 1;
  }

  llcpp::fuchsia::boot::WriteOnlyLog::SyncClient log_client;
  status = ConnectToWriteLog(&log_client);
  if (status != ZX_OK) {
    fprintf(stderr, "console-launcher: failed to get write log: %s\n",
            zx_status_get_string(status));
    return 1;
  }

  std::optional<console_launcher::Arguments> args = console_launcher::GetArguments(&boot_args);
  if (!args) {
    fprintf(stderr, "console-launcher: Failed to get arguments\n");
    return 1;
  }

  // Past this point we should be using logging instead of stdout.
  if (args->log_to_debuglog) {
    zx_status_t status = log_to_debuglog(&log_client);
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to reconfigure logger to use debuglog: %s\n",
              zx_status_get_string(status));
      return status;
    }
  }

  status = console_launcher::SetupVirtcon(&boot_args);
  if (status != ZX_OK) {
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
    return result.status_value();
  }
  auto& launcher = result.value();
  LOGF(INFO, "console.shell: enabled");

  autorun::AutoRun autorun;
  if (!args->autorun_boot.empty()) {
    auto result = log_client.Get();
    if (result.status() != ZX_OK) {
      LOGF(ERROR, "console-launcher: failed to get debuglog '%s'", result.status_string());
      return result.status();
    }
    status = autorun.SetupBootCmd(args->autorun_boot, launcher.shell_job(), std::move(result->log));
    if (status != ZX_OK) {
      LOGF(ERROR, "Autorun: Failed to setup boot command: %s", zx_status_get_string(status));
    }
  }
  if (!args->autorun_system.empty()) {
    auto result = log_client.Get();
    if (result.status() != ZX_OK) {
      LOGF(ERROR, "console-launcher: failed to get debuglog '%s'", result.status_string());
      return result.status();
    }
    status =
        autorun.SetupSystemCmd(args->autorun_system, launcher.shell_job(), std::move(result->log));
    if (status != ZX_OK) {
      LOGF(ERROR, "Autorun: Failed to setup system command: %s", zx_status_get_string(status));
    }
  }

  while (true) {
    status = launcher.LaunchShell(*args);
    if (status != ZX_OK) {
      LOGF(ERROR, "console-launcher: failed to launch shell '%s'", zx_status_get_string(status));
      return 1;
    }

    status = launcher.WaitForShellExit();
    if (status != ZX_OK) {
      LOGF(ERROR, "console-launcher: failed to wait for shell exit: '%s'",
           zx_status_get_string(status));
      return 1;
    }
  }
}
