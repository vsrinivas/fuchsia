// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/integration-test-base.h"

#include <lib/fdio/spawn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

namespace fuzzing {

ZxResult<> IntegrationTestBase::Start(const std::string& path, zx::channel registrar) {
  const char* argv[2] = {path.c_str(), nullptr};
  fdio_spawn_action_t actions[] = {
      {
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_USER0, 0),
                  .handle = registrar.release(),
              },
      },
  };
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  auto status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr,
                               sizeof(actions) / sizeof(actions[0]), actions,
                               process_.reset_and_get_address(), err_msg);
  EXPECT_EQ(status, ZX_OK) << err_msg;
  return AsZxResult(status);
}

ZxPromise<> IntegrationTestBase::AwaitTermination() {
  return fpromise::make_promise([this, terminated = ZxFuture<zx_packet_signal_t>()](
                                    Context& context) mutable -> ZxResult<> {
    auto status = process_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite_past(), nullptr);
    if (status == ZX_OK) {
      return fpromise::ok();
    }
    if (status != ZX_ERR_TIMED_OUT) {
      FX_LOGS(WARNING) << "failed to check if process terminated: " << zx_status_get_string(status);
      return fpromise::error(status);
    }
    if (!terminated) {
      terminated = executor()->MakePromiseWaitHandle(zx::unowned_handle(process_.get()),
                                                     ZX_PROCESS_TERMINATED);
    }
    if (!terminated(context)) {
      return fpromise::pending();
    }
    if (terminated.is_error()) {
      FX_LOGS(WARNING) << "failed to wait for process to terminate: "
                       << zx_status_get_string(status);
      return fpromise::error(terminated.error());
    }
    zx_info_process_t info;
    status = process_.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "failed to get info from terminated process: "
                       << zx_status_get_string(status);
      return fpromise::error(status);
    }
    EXPECT_EQ(info.return_code, 0);
    process_.reset();
    return fpromise::ok();
  });
}

void IntegrationTestBase::TearDown() {
  process_.kill();
  AsyncTest::TearDown();
}

}  // namespace fuzzing
