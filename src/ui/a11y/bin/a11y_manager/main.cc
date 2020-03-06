// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <trace-provider/provider.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/bin/a11y_manager/app.h"

int main(int argc, const char** argv) {
  syslog::InitLogger();
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto context = sys::ComponentContext::Create();
  a11y::ViewManager view_manager(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                                 context->outgoing()->debug_dir());
  a11y::TtsManager tts_manager(context.get());
  a11y::ColorTransformManager color_transform_manager(context.get());

  a11y_manager::App app(context.get(), &view_manager, &tts_manager, &color_transform_manager);
  loop.Run();
  return 0;
}
