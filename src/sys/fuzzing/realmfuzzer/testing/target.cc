// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/testing/target.h"

#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/exception.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include "src/sys/fuzzing/realmfuzzer/testing/target-main.h"

namespace fuzzing {

TestTarget::TestTarget(ExecutorPtr executor) : executor_(executor), target_(executor) {}

TestTarget::~TestTarget() { Reset(); }

zx::process TestTarget::Launch() {
  Reset();

  // First, create the channel between this object and the new process.
  zx::channel remote;
  auto status = zx::channel::create(0, &local_, &remote);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  target_.AddArg("bin/realmfuzzer_test_target");
  target_.AddChannel(kTestChannelId, std::move(remote));
  status = target_.Spawn();
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  // Install a process-debug exception handler. This will receive new exceptions before the process
  // exception handler that we want to test, so on the first pass simply set the "second-chance"
  // strategy, and on receiving them again, simply kill the process to suppress further handling.
  zx::process process;
  status = target_.Duplicate(&process);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  zx::channel channel;
  status = process.create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &channel);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);

  // If this task produces an error, then the process exited and channel was closed before or during
  // the wait and/or read. |GetResult| will attempt to determine the reason using the exitcode.
  auto task =
      fpromise::make_promise([this, channel = std::move(channel),
                              crash = ZxFuture<zx_packet_signal_t>()](
                                 Context& context) mutable -> ZxResult<> {
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
            return fpromise::ok();
          } else {
            strategy = ZX_EXCEPTION_STRATEGY_SECOND_CHANCE;
            exception.set_property(ZX_PROP_EXCEPTION_STRATEGY, &strategy, sizeof(strategy));
          }
        }
      })
          .and_then(target_.Kill())
          .wrap_with(scope_);
  FX_DCHECK(executor_);
  executor_->schedule_task(std::move(task));

  zx_info_handle_basic_t info;
  status = process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
  id_ = info.koid;

  return process;
}

ZxPromise<> TestTarget::Crash() {
  // Resetting the channel will trigger an |FX_CHECK| in the target process. Tests that use this
  // method must suppress fatal log message being treated as test failures.
  return fpromise::make_promise([this]() -> ZxResult<> {
           local_.reset();
           return fpromise::ok();
         })
      .and_then(target_.Wait())
      .and_then([](const int64_t& ignored) -> ZxResult<> { return fpromise::ok(); })
      .wrap_with(scope_);
}

ZxPromise<> TestTarget::Exit(int32_t exitcode) {
  return fpromise::make_promise([this, exitcode]() -> ZxResult<> {
           return AsZxResult(local_.write(0, &exitcode, sizeof(exitcode), nullptr, 0));
         })
      .and_then(target_.Wait())
      .and_then([](const int64_t& ignored) -> ZxResult<> { return fpromise::ok(); })
      .wrap_with(scope_);
}

void TestTarget::Reset() {
  target_.Kill();
  local_.reset();
  target_.Reset();
}

}  // namespace fuzzing
