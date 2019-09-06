// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#ifndef NTRACE
#include <trace-provider/provider.h>
#endif

#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/command_line_options.h"
#include "src/media/audio/audio_core/reporter.h"

static constexpr size_t kNumIoDispatcherThreads = 1;

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
#ifndef NTRACE
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
#endif

  // Initialize our telemetry reporter (which optimizes to nothing if ENABLE_REPORTER is set to 0).
  auto component_context = sys::ComponentContext::Create();
  REP(Init(component_context.get()));

  auto options = media::audio::CommandLineOptions::ParseFromArgcArgv(argc, argv);
  if (!options.is_ok()) {
    return -1;
  }

  // We spin up a dispatcher appropriate for executing long-running or potentially blocking
  // operations
  async::Loop io_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  for (size_t i = 0; i < kNumIoDispatcherThreads; ++i) {
    auto thread_name = "io-thread-" + std::to_string(i);
    io_loop.StartThread(thread_name.c_str());
  }
  media::audio::AudioCoreImpl impl(loop.dispatcher(), io_loop.dispatcher(),
                                   std::move(component_context), options.take_value());
  loop.Run();

  // Post a shutdown task to allow any runnable tasks in the loop to have a chance to execute.
  async::PostTask(io_loop.dispatcher(), [&io_loop] { io_loop.Shutdown(); });
  io_loop.JoinThreads();
  return 0;
}
