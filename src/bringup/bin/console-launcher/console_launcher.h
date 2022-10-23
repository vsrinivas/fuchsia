// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_
#define SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/result.h>

#include <optional>
#include <string>

#include <fbl/unique_fd.h>

namespace console_launcher {

zx::result<fbl::unique_fd> WaitForFile(const char* path, zx::time deadline);

struct Device {
  std::string path = "/svc/console";
  bool is_virtio = false;
};

struct Arguments {
  bool run_shell = true;
  bool virtcon_disable = false;
  std::string autorun_boot;
  std::string autorun_system;

  Device device;
  std::string term = "TERM=";
  bool virtual_console_need_debuglog = false;
};

zx::result<Arguments> GetArguments(const fidl::ClientEnd<fuchsia_boot::Arguments>& client);

class ConsoleLauncher {
 public:
  static zx::result<ConsoleLauncher> Create();
  zx::result<zx::process> LaunchShell(fidl::ClientEnd<fuchsia_io::Directory> root,
                                      fidl::ClientEnd<fuchsia_hardware_pty::Device> stdio,
                                      const std::string& term,
                                      const std::optional<std::string>& cmd) const;

  const zx::job& shell_job() const { return shell_job_; }

 private:
  // WARNING: This job is created directly from the root job with no additional job policy
  // restrictions. We only create it when 'console.shell' is enabled to help protect against
  // accidental usage.
  zx::job shell_job_;
};

zx_status_t WaitForExit(zx::process process);

}  // namespace console_launcher

#endif  // SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_CONSOLE_LAUNCHER_H_
