// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/thread.h>

#include <memory>

#include <trace-provider/provider.h>

#include "lib/inspect/cpp/inspect.h"
#include "lib/sys/inspect/cpp/component.h"
#include "src/lib/fsl/syslogger/init.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/bin/app.h"
#include "src/ui/scenic/lib/scenic/util/scheduler_profile.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;
  if (fsl::InitLoggerFromCommandLine(command_line, {"scenic"}) != ZX_OK)
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> app_context(sys::ComponentContext::Create());

  // Set up an inspect::Node to inject into the App.
  sys::ComponentInspector inspector(app_context.get());
  scenic_impl::App app(std::move(app_context), inspector.root().CreateChild("scenic"),
                       [&loop] { loop.Quit(); });

  // TODO(40858): Migrate to the role-based scheduler API when available,
  // instead of hard coding parameters.
  {
    // TODO(44209): Centralize default frame period.
    const zx::duration capacity = zx::msec(16);
    const zx::duration deadline = zx::msec(16);
    const zx::duration period = deadline;
    const auto profile = util::GetSchedulerProfile(capacity, deadline, period);
    if (profile) {
      const auto status = zx::thread::self()->set_profile(profile, 0);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to apply profile to main thread: " << status;
      }
    }
  }

  loop.Run();
  FXL_LOG(INFO) << "Quit main Scenic loop.";

  return 0;
}
