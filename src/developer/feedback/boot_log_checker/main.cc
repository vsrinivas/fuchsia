// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include <string>

#include "src/developer/feedback/boot_log_checker/reboot_log_handler.h"
#include "src/lib/syslog/cpp/logger.h"

int main(int argc, char** argv) {
  syslog::InitLogger({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  auto context = sys::ComponentContext::Create();

  const char reboot_log[] = "/boot/log/last-panic.txt";
  auto promise = feedback::HandleRebootLog(reboot_log, loop.dispatcher(), context->svc())
                     .then([&reboot_log, &loop](const fit::result<void>& result) {
                       if (result.is_error()) {
                         FX_LOGS(ERROR)
                             << "Failed to handle reboot log at " << reboot_log << ". Won't retry.";
                       }
                       // The delay is used to guarantee that we are not exiting the process before
                       // Cobalt had time to receive and send its events.
                       // We delay asynchronously to avoid blocking the component's out directory.
                       // TODO(fxb/47645): remove delay.
                       async::PostDelayedTask(
                           loop.dispatcher(), [&loop] { loop.Quit(); }, zx::sec(30));
                     });

  executor.schedule_task(std::move(promise));
  loop.Run();

  return EXIT_SUCCESS;
}
