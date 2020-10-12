// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_
#define SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_

#include <fuchsia/boot/llcpp/fidl.h>
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
};

std::optional<Arguments> GetArguments(llcpp::fuchsia::boot::Arguments::SyncClient* client);

class ConsoleLauncher {
 public:
  static zx::status<ConsoleLauncher> Create();
  zx_status_t Init();
  zx_status_t LaunchShell(const Arguments& args);
  zx_status_t WaitForShellExit();

 private:
  // If the console is a virtio connection, then speak the fuchsia.hardware.virtioconsole.Device
  // interface to get the real fuchsia.io.File connection
  std::optional<fbl::unique_fd> GetVirtioFd(const Arguments& args, fbl::unique_fd device_fd);

  zx::process shell_process_;
  // TODO(fxbug.dev/33957): Remove all uses of the root job.
  zx::job root_job_;
};

}  // namespace console_launcher

#endif  // SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_
