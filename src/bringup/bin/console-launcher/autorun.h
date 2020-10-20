// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_AUTORUN_H_
#define SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_AUTORUN_H_

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/zx/job.h>
#include <zircon/status.h>

#include <string>
#include <thread>

namespace autorun {

class AutoRun {
 public:
  ~AutoRun();

  // Setup and run the boot command.
  // Launches the given command under the given job and gives it a STDIO handle.
  zx_status_t SetupBootCmd(std::string cmd, const zx::job& job, zx::handle stdio);

  // Setup and run the system command.
  // Launches the given command under the given job and gives it a STDIO handle.
  // The system command is only run after /sytem-delayed appears.
  zx_status_t SetupSystemCmd(std::string cmd, const zx::job& job, zx::handle stdio);

 private:
  // The thread running the boot_cmd.
  std::thread boot_thread_;
  // The thread running the system_cmd.
  std::thread system_thread_;
};

}  // namespace autorun

#endif  // SRC_BRINGUP_BIN_CONSOLE_LAUNCHER_AUTORUN_H_
