// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <trace-provider/provider.h>

#include "src/developer/memory/monitor/monitor.h"
#include "src/lib/fsl/syslogger/init.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;
  fsl::InitLoggerFromCommandLine(command_line, {"memory_monitor"});

  FXL_VLOG(2) << argv[0] << ": starting";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), monitor::Monitor::kTraceName);
  std::unique_ptr<sys::ComponentContext> startup_context = sys::ComponentContext::Create();

  // Lower the priority.
  fuchsia::scheduler::ProfileProviderSyncPtr profile_provider;
  startup_context->svc()->Connect<fuchsia::scheduler::ProfileProvider>(
      profile_provider.NewRequest());
  zx_status_t fidl_status;
  zx::profile profile;
  auto status = profile_provider->GetProfile(8 /* LOW_PRIORITY */, "memory_monitor.cmx",
                                             &fidl_status, &profile);
  FXL_CHECK(status == ZX_OK);
  FXL_CHECK(fidl_status == ZX_OK);
  auto set_status = zx_object_set_profile(zx_thread_self(), profile.get(), 0);
  FXL_CHECK(set_status == ZX_OK);

  monitor::Monitor app(std::move(startup_context), command_line, loop.dispatcher(),
                       true /* send_metrics */, true /* watch_memory_pressure */);
  loop.Run();

  FXL_VLOG(2) << argv[0] << ": exiting";

  return 0;
}
