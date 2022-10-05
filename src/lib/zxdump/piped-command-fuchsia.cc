// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/job.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <vector>

#include "piped-command.h"

namespace zxdump {

fit::result<std::string> PipedCommand::StartArgv(cpp20::span<const char*> argv) {
  ZX_DEBUG_ASSERT(!process_);

  std::vector<fdio_spawn_action_t> actions = spawn_actions_;
  actions.push_back({.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {argv[0]}});

  for (auto& [number, fd] : redirect_) {
    actions.push_back({.action = FDIO_SPAWN_ACTION_TRANSFER_FD,
                       .fd = {.local_fd = fd.release(), .target_fd = number}});
  }

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = fdio_spawn_etc(zx::job::default_job()->get(), FDIO_SPAWN_CLONE_ALL, argv[0],
                                      argv.data(), environ, actions.size(), actions.data(),
                                      process_.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    return fit::error{err_msg};
  }
  return fit::ok();
}

PipedCommand::~PipedCommand() {
  if (process_) {
    zx_signals_t signaled;
    zx_status_t status = process_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &signaled);
    ZX_ASSERT_MSG(status == 0, "wait_one: %s", zx_status_get_string(status));
    ZX_DEBUG_ASSERT(signaled & ZX_PROCESS_TERMINATED);
  }
}

}  // namespace zxdump
