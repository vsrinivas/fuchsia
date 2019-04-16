// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/service_directory.h>
#include <trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/appmgr/appmgr.h"
#include "src/lib/fxl/command_line.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);

  auto environment_services = sys::ServiceDirectory::CreateFromNamespace();

  trace::TraceProvider trace_provider(loop.dispatcher());

  component::AppmgrArgs args{
      .pa_directory_request = std::move(request),
      .environment_services = environment_services,
      .sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx",
      .sysmgr_args = {},
      .run_virtual_console = !command_line.HasOption("disable-virtual-console"),
      .retry_sysmgr_crash = true};
  component::Appmgr appmgr(loop.dispatcher(), std::move(args));

  loop.Run();
  return 0;
}
