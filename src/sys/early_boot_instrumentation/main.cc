// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/sys/early_boot_instrumentation/coverage_source.h"

int main(int argc, char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line, {"early-boot-instrumentation"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Add prof_data dir.
  auto* prof_data_dir = context->outgoing()->GetOrCreateDirectory("prof-data");
  auto static_prof = std::make_unique<vfs::PseudoDir>();
  auto dynamic_prof = std::make_unique<vfs::PseudoDir>();

  // Even if we fail to populate from the sources, we expose empty directories,
  // such that the contract remains.
  fbl::unique_fd kernel_data_dir(open("/boot/kernel/data", O_RDONLY));
  if (!kernel_data_dir) {
    const char* err = strerror(errno);
    FX_LOGS(ERROR) << "Could not obtain handle to '/boot/kernel/data'. " << err;
  }

  if (auto res = early_boot_instrumentation::ExposeKernelProfileData(kernel_data_dir, *static_prof);
      res.is_error()) {
    FX_LOGS(ERROR) << "Could not expose kernel profile data. " << res.status_value();
  }

  // outgoing/prof-data/static
  prof_data_dir->AddEntry("static", std::move(static_prof));
  // outgoing/prof-data/dynamic
  prof_data_dir->AddEntry("dynamic", std::move(dynamic_prof));
  loop.Run();
  return 0;
}
