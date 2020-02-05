// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <trace-provider/provider.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/bin/a11y_tts_log_engine/log_engine.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"a11y_tts_log_engine"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  auto context = sys::ComponentContext::Create();
  a11y::LogEngine app(std::move(context));
  loop.Run();
  return 0;
}
