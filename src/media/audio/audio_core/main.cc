// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#ifndef NTRACE
#include <trace-provider/provider.h>
#endif

#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/command_line_options.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/threading_model.h"

using namespace media::audio;

int main(int argc, const char** argv) {
  auto threading_model = ThreadingModel::CreateWithMixStrategy(MixStrategy::kThreadPerMix);
#ifndef NTRACE
  trace::TraceProviderWithFdio trace_provider(threading_model->FidlDomain().dispatcher());
#endif

  // Initialize our telemetry reporter (which optimizes to nothing if ENABLE_REPORTER is set to 0).
  auto component_context = sys::ComponentContext::Create();
  REP(Init(component_context.get()));

  auto options = CommandLineOptions::ParseFromArgcArgv(argc, argv);
  if (!options.is_ok()) {
    return -1;
  }

  AudioCoreImpl impl(threading_model.get(), std::move(component_context), options.take_value());
  threading_model->RunAndJoinAllThreads();
  return 0;
}
