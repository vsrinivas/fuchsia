// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_
#define SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>

#include <optional>
#include <string>

#include <fbl/unique_fd.h>

namespace console_launcher {

struct Arguments {
  bool run_shell = true;
  bool is_virtio = false;
  bool log_to_debuglog = false;
  std::string term = "TERM=";
  std::string device = "/svc/console";
  std::string autorun_boot;
  std::string autorun_system;
};

zx::status<Arguments> GetArguments(const fidl::ClientEnd<fuchsia_boot::Arguments>& client);

class ConsoleLauncher {
 public:
  static zx::status<ConsoleLauncher> Create();
  zx_status_t LaunchShell(const Arguments& args);
  zx_status_t WaitForShellExit();

  zx::job& shell_job() { return shell_job_; }

 private:
  zx::process shell_process_;
  // WARNING: This job is created directly from the root job with no additional job policy
  // restrictions. We only create it when 'console.shell' is enabled to help protect against
  // accidental usage.
  zx::job shell_job_;
};

}  // namespace console_launcher

#endif  // SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_
