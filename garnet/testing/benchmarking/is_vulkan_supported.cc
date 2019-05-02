// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/testing/benchmarking/is_vulkan_supported.h"

#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include "src/lib/fxl/logging.h"

#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

namespace benchmarking {

bool IsVulkanSupported() {
  std::vector<std::string> command = {
      // clang-format off
      "/pkgfs/packages/run/0/bin/run",
      "fuchsia-pkg://fuchsia.com/vulkan_is_supported#meta/vulkan_is_supported.cmx",
      // clang-format on
  };

  std::vector<const char*> raw_command;
  for (const auto& arg : command) {
    raw_command.push_back(arg.c_str());
  }
  raw_command.push_back(nullptr);

  int pipefd[2];
  int pipe_status = pipe(pipefd);
  FXL_CHECK(pipe_status == 0);

  std::vector<fdio_spawn_action_t> actions = {
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = pipefd[1], .target_fd = 1}}};

  zx::handle subprocess;
  zx_status_t status = fdio_spawn_etc(
      ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, raw_command[0],
      raw_command.data(), nullptr, actions.size(), actions.data(),
      subprocess.reset_and_get_address(), nullptr);
  FXL_CHECK(status == ZX_OK);

  zx_signals_t signals_observed = 0;
  status = subprocess.wait_one(ZX_TASK_TERMINATED, zx::time(ZX_TIME_INFINITE),
                               &signals_observed);
  FXL_CHECK(status == ZX_OK);
  zx_info_process_t proc_info;
  status = subprocess.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                               nullptr, nullptr);
  FXL_CHECK(status == ZX_OK);

  FXL_CHECK(proc_info.return_code == 0);
  int close_result = close(pipefd[1]);
  FXL_CHECK(close_result == 0);

  std::optional<char> first_char;
  char c;
  if (read(pipefd[0], &c, sizeof(c)) > 0) {
    first_char = c;
  }

  close_result = close(pipefd[0]);
  FXL_CHECK(close_result == 0);

  if (first_char && *first_char == '1') {
    return true;
  } else if (first_char && *first_char == '0') {
    return false;
  } else {
    FXL_LOG(ERROR) << "Failed to run `vulkan_is_supported`";
    exit(1);
  }
}

}  // namespace benchmarking
