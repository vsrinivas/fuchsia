// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/syslogger/init.h>
#include <lib/inspect/inspect.h>
#include <lib/sys/cpp/component_context.h>
#include <trace-provider/provider.h>

#include <memory>

#include "garnet/bin/ui/scenic/app.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;
  if (fsl::InitLoggerFromCommandLine(command_line, {"scenic"}) != ZX_OK)
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> app_context(
      sys::ComponentContext::Create());

  // Set up an inspect::Node to inject into the App.
  auto object_dir = component::ObjectDir(component::Object::Make("objects"));
  fidl::BindingSet<fuchsia::inspect::Inspect> inspect_bindings;
  app_context->outgoing()->GetOrCreateDirectory("objects")->AddEntry(
      fuchsia::inspect::Inspect::Name_,
      std::make_unique<vfs::Service>(
          inspect_bindings.GetHandler(object_dir.object().get())));

  scenic_impl::App app(app_context.get(), inspect::Node(std::move(object_dir)),
                       [&loop] { loop.Quit(); });

  loop.Run();

  return 0;
}
