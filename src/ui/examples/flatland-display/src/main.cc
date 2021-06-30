// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/time.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto component_context = sys::ComponentContext::Create();

  auto flatland = component_context->svc()->Connect<fuchsia::ui::composition::Flatland>();
  auto flatland_display =
      component_context->svc()->Connect<fuchsia::ui::composition::FlatlandDisplay>();

  constexpr fuchsia::ui::composition::TransformId kRootTransformId{.value = 1};

  flatland->CreateTransform(kRootTransformId);
  flatland->SetRootTransform(kRootTransformId);

  // TODO(fxbug.dev/76640): add content to bring this to parity with the Rust version.
  FX_LOGS(ERROR) << "flatland-display C++ example doesn't do anything yet.";

  loop.Run();
  return 0;
}
