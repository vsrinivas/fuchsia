// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/target.h"

#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

namespace fuzzing {

TestTarget::~TestTarget() { Reset(); }

zx::process TestTarget::Launch() {
  Reset();

  // First, create the channel between this object and the new process.
  zx::channel remote;
  auto status = zx::channel::create(0, &local_, &remote);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  // Spawn the new process in the new job.
  const char* argv[2] = {"/pkg/bin/component_fuzzing_test_target", nullptr};
  fdio_spawn_action_t actions[] = {
      {
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_USER0, 0),
                  .handle = remote.release(),
              },
      },
  };
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr,
                          sizeof(actions) / sizeof(actions[0]), actions,
                          process_.reset_and_get_address(), err_msg);
  FX_DCHECK(status == ZX_OK) << err_msg;

  // Install a process-debug exception handler. This will receive new exceptions before the process
  // exception handler that we want to test, so on the first pass simply set the "second-chance"
  // strategy, and on receiving them again, simply kill the process to suppress further handling.
  status = process_.create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &exception_channel_);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  exception_thread_ = std::thread([this]() {
    while (exception_channel_.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr) ==
           ZX_OK) {
      zx::exception exception;
      zx_exception_info_t info;
      uint32_t strategy;
      if (zx_channel_read(exception_channel_.get(), 0, &info, exception.reset_and_get_address(),
                          sizeof(info), 1, nullptr, nullptr) != ZX_OK ||
          !exception.is_valid() ||
          exception.get_property(ZX_PROP_EXCEPTION_STRATEGY, &strategy, sizeof(strategy)) !=
              ZX_OK) {
        continue;
      }
      if (strategy == ZX_EXCEPTION_STRATEGY_SECOND_CHANCE) {
        process_.kill();
      } else {
        strategy = ZX_EXCEPTION_STRATEGY_SECOND_CHANCE;
        exception.set_property(ZX_PROP_EXCEPTION_STRATEGY, &strategy, sizeof(strategy));
      }
    }
  });

  // Return a copy of the process.
  zx::process copy;
  status = process_.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  return copy;
}

void TestTarget::Crash() {
  // Resetting the channel will trigger an |FX_CHECK| in the target process. Tests that use this
  // method must suppress fatal log message being treated as test failures.
  local_.reset();
}

void TestTarget::Exit(int32_t exitcode) {
  auto status = local_.write(0, &exitcode, sizeof(exitcode), nullptr, 0);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

void TestTarget::Join() {
  auto status = process_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

void TestTarget::Reset() {
  process_.kill();
  exception_channel_.reset();
  if (exception_thread_.joinable()) {
    exception_thread_.join();
  }
  local_.reset();
  process_.reset();
}

}  // namespace fuzzing
