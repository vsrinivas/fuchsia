// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
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
                       loop.Quit();
                     });

  // We delay asynchronously filing the crash report by 30 seconds so that memory_monitor has time
  // to get picked up by the Inspect service and its data is included in the bugreport.zip generated
  // by feedback_agent. The data is critical to debug OOM crash reports.
  // TODO(fxb/46216): remove delay.
  zx::nanosleep(zx::deadline_after(zx::sec(30)));
  executor.schedule_task(std::move(promise));
  loop.Run();

  return EXIT_SUCCESS;
}
