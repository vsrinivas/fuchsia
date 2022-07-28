// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/process.h"

#include <lib/fdio/spawn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iomanip>

#include "src/sys/fuzzing/common/testing/runner.h"

namespace fuzzing {

zx_status_t StartProcess(const std::string& relpath, const component::FuchsiaPkgUrl& url,
                         std::vector<zx::channel> channels, zx::process* out) {
  auto path = std::string("/pkg/bin/") + relpath;
  auto url_str = url.ToString();
  const char* argv[] = {path.c_str(), url_str.c_str(), nullptr};
  uint16_t i = 0;
  std::vector<fdio_spawn_action_t> actions;
  for (auto& channel : channels) {
    fdio_spawn_action_t action{};
    action.action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    action.h.id = PA_HND(PA_USER0, i++);
    action.h.handle = channel.release();
    actions.emplace_back(std::move(action));
  }
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  if (auto status =
          fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr,
                         actions.size(), actions.data(), out->reset_and_get_address(), err_msg);
      status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to start '" << path << "': " << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

ZxPromise<> AwaitTermination(zx::process process, ExecutorPtr executor) {
  return fpromise::make_promise([executor, process = std::move(process),
                                 terminated = ZxFuture<zx_packet_signal_t>()](
                                    Context& context) mutable -> ZxResult<> {
    auto status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite_past(), nullptr);
    if (status == ZX_OK) {
      return fpromise::ok();
    }
    if (status != ZX_ERR_TIMED_OUT) {
      FX_LOGS(WARNING) << "Failed to check if process terminated: " << zx_status_get_string(status);
      return fpromise::error(status);
    }
    if (!terminated) {
      terminated =
          executor->MakePromiseWaitHandle(zx::unowned_handle(process.get()), ZX_PROCESS_TERMINATED);
    }
    if (!terminated(context)) {
      return fpromise::pending();
    }
    if (terminated.is_error()) {
      FX_LOGS(WARNING) << "Failed to wait for process to terminate: "
                       << zx_status_get_string(status);
      return fpromise::error(terminated.error());
    }
    zx_info_process_t info;
    status = process.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to get info from terminated process: "
                       << zx_status_get_string(status);
      return fpromise::error(status);
    }
    if (info.return_code != 0) {
      FX_LOGS(WARNING) << "Process exited with code: " << info.return_code;
      return fpromise::error(ZX_ERR_BAD_STATE);
    }
    return fpromise::ok();
  });
}

}  // namespace fuzzing
