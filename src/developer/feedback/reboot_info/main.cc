// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include <string>

#include "src/developer/feedback/reboot_info/main_service.h"
#include "src/developer/feedback/reboot_info/reboot_log.h"
#include "src/developer/feedback/reboot_info/reporter.h"
#include "src/lib/syslog/cpp/logger.h"

int main(int argc, char** argv) {
  syslog::SetTags({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  const feedback::RebootLog reboot_log =
      feedback::RebootLog::ParseRebootLog("/boot/log/last-panic.txt");

  feedback::MainService main_service(reboot_log);

  // fuchsia.feedback.LastRebootInfoProvider
  context->outgoing()->AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::LastRebootInfoProvider>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
            main_service.HandleLastRebootInfoProviderRequest(std::move(request));
          }));

  feedback::Reporter reporter(loop.dispatcher(), context->svc());
  // We file the crash report with a 90s delay to increase the likelihood that Inspect data (at all
  // and specifically the data from memory_monitor) is included in the bugreport.zip generated by
  // the Feedback service. The memory_monitor Inspect data is critical to debug OOM crash reports.
  // TODO(fxb/46216, fxb/48485): remove delay.
  reporter.ReportOn(reboot_log, /*crash_reporting_delay=*/zx::sec(90));

  loop.Run();

  return EXIT_SUCCESS;
}
