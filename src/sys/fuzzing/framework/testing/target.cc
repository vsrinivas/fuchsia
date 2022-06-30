// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/target.h"

#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/exception.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

namespace fuzzing {

TestTarget::TestTarget(ExecutorPtr executor) : executor_(std::move(executor)) {}

TestTarget::~TestTarget() { Reset(); }

zx::process TestTarget::Launch() {
  Reset();

  // First, create the channel between this object and the new process.
  zx::channel remote;
  auto status = zx::channel::create(0, &local_, &remote);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  // Spawn the new process in the new job.
  const char* argv[2] = {"/pkg/bin/component_fuzzing_framework_test_target", nullptr};
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
  zx::channel channel;
  status = process_.create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &channel);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  // If this task produces an error, then the process exited and channel was closed before or during
  // the wait and/or read. |GetResult| will attempt to determine the reason using the exitcode.
  auto task =
      fpromise::make_promise([this, channel = std::move(channel),
                              crash = ZxFuture<zx_packet_signal_t>()](
                                 Context& context) mutable -> Result<> {
        while (true) {
          if (!crash) {
            crash = executor_->MakePromiseWaitHandle(zx::unowned_handle(channel.get()),
                                                     ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
          }
          if (!crash(context)) {
            return fpromise::pending();
          }
          if (crash.is_error()) {
            return fpromise::ok();
          }
          auto packet = crash.take_value();
          if ((packet.observed & ZX_CHANNEL_READABLE) == 0) {
            return fpromise::ok();
          }
          zx::exception exception;
          zx_exception_info_t info;
          uint32_t strategy;
          if (channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1, nullptr,
                           nullptr) != ZX_OK ||
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
      }).wrap_with(scope_);
  FX_DCHECK(executor_);
  executor_->schedule_task(std::move(task));

  zx_info_handle_basic_t info;
  status = process_.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
  id_ = info.koid;

  // Return a copy of the process.
  zx::process process;
  status = process_.duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
  FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
  return process;
}

ZxPromise<> TestTarget::Crash() {
  // Resetting the channel will trigger an |FX_CHECK| in the target process. Tests that use this
  // method must suppress fatal log message being treated as test failures.
  return fpromise::make_promise([this]() -> ZxResult<> {
           local_.reset();
           return fpromise::ok();
         })
      .and_then(AwaitTermination())
      .wrap_with(scope_);
}

ZxPromise<> TestTarget::Exit(int32_t exitcode) {
  return fpromise::make_promise([this, exitcode]() -> ZxResult<> {
           return AsZxResult(local_.write(0, &exitcode, sizeof(exitcode), nullptr, 0));
         })
      .and_then(AwaitTermination())
      .wrap_with(scope_);
}

ZxPromise<> TestTarget::AwaitTermination() {
  return executor_->MakePromiseWaitHandle(zx::unowned_handle(process_.get()), ZX_PROCESS_TERMINATED)
      .and_then([](const zx_packet_signal_t& packet) { return fpromise::ok(); })
      .wrap_with(scope_);
}

void TestTarget::Reset() {
  process_.kill();
  local_.reset();
  process_.reset();
}

}  // namespace fuzzing
