// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <future>

#include "src/bringup/bin/console-launcher/autorun.h"
#include "src/bringup/bin/console-launcher/console_launcher.h"
#include "src/bringup/bin/console-launcher/virtcon-setup.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

int main(int argv, char** argc) {
  syslog::SetTags({"console-launcher"});

  if (zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
    FX_PLOGS(ERROR, status)
        << "failed to redirect stdout to debuglog, assuming test environment and continuing";
  }

  FX_LOGS(INFO) << "running";

  zx::status boot_args = service::Connect<fuchsia_boot::Arguments>();
  if (boot_args.is_error()) {
    FX_PLOGS(ERROR, boot_args.status_value())
        << "failed to connect to " << fidl::DiscoverableProtocolName<fuchsia_boot::Arguments>;
    return 1;
  }

  zx::status args = console_launcher::GetArguments(boot_args.value());
  if (args.is_error()) {
    FX_PLOGS(ERROR, args.status_value()) << "failed to get arguments";
    return 1;
  }

  zx::status log_client_end = service::Connect<fuchsia_boot::WriteOnlyLog>();
  if (log_client_end.is_error()) {
    FX_PLOGS(ERROR, log_client_end.status_value())
        << "failed to connect to " << fidl::DiscoverableProtocolName<fuchsia_boot::WriteOnlyLog>;
    return 1;
  }
  fidl::WireSyncClient log_client = fidl::BindSyncClient(std::move(log_client_end).value());

  if (zx_status_t status = console_launcher::SetupVirtcon(boot_args.value()); status != ZX_OK) {
    // If launching virtcon fails, we still should continue so that the autorun programs
    // and serial console are launched.
    FX_PLOGS(ERROR, status) << "failed to set up virtcon";
  }

  if (!args->run_shell) {
    if (!args->autorun_boot.empty()) {
      FX_LOGS(ERROR) << "cannot launch autorun command '" << args->autorun_boot << "'";
    }
    FX_LOGS(INFO) << "console.shell: disabled";
    // TODO(https://fxbug.dev/97657): Hang around. If we exit before archivist has started, our logs
    // will be lost, and this log is load bearing in shell_disabled_test.
    std::promise<void>().get_future().wait();
    return 0;
  }
  FX_LOGS(INFO) << "console.shell: enabled";

  zx::status result = console_launcher::ConsoleLauncher::Create();
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.status_value()) << "failed to create console launcher";
    return 1;
  }
  auto& launcher = result.value();

  autorun::AutoRun autorun;
  if (!args->autorun_boot.empty()) {
    fidl::WireResult result = log_client->Get();
    if (!result.ok()) {
      FX_PLOGS(ERROR, result.status()) << "failed to get debuglog";
      return 1;
    }
    if (zx_status_t status = autorun.SetupBootCmd(args->autorun_boot, launcher.shell_job(),
                                                  std::move(result.value().log));
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to set up boot command '" << args->autorun_boot << "'";
    }
  }
  if (!args->autorun_system.empty()) {
    fidl::WireResult result = log_client->Get();
    if (!result.ok()) {
      FX_PLOGS(ERROR, result.status()) << "failed to get debuglog";
      return 1;
    }
    if (zx_status_t status = autorun.SetupSystemCmd(args->autorun_system, launcher.shell_job(),
                                                    std::move(result.value().log));
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to set up system command '" << args->autorun_system << "'";
    }
  }

  while (true) {
    if (zx_status_t status = launcher.LaunchShell(args.value()); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to launch shell";
      return 1;
    }

    if (zx_status_t status = launcher.WaitForShellExit(); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to wait for shell exit";
      return 1;
    }
  }
}
