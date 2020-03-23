// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#ifndef NTRACE
#include <trace-provider/provider.h>
#endif

#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/plug_detector.h"
#include "src/media/audio/audio_core/process_config_loader.h"
#include "src/media/audio/audio_core/profile_provider.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/thermal_agent.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/lib/logging/logging.h"

using namespace media::audio;

constexpr char kProcessConfigPath[] = "/config/data/audio_core_config.json";

int main(int argc, const char** argv) {
  auto threading_model = ThreadingModel::CreateWithMixStrategy(MixStrategy::kThreadPerMix);
#ifndef NTRACE
  trace::TraceProviderWithFdio trace_provider(threading_model->FidlDomain().dispatcher());
#endif

#ifdef NDEBUG
  Logging::Init(FX_LOG_WARNING, {"audio_core"});
#else
  // For verbose logging, set to -media::audio::TRACE or -media::audio::SPEW
  Logging::Init(FX_LOG_INFO, {"audio_core"});
#endif

  FX_LOGS(INFO) << "AudioCore starting up";

  // Initialize our telemetry reporter (which optimizes to nothing if ENABLE_REPORTER is set to 0).
  auto component_context = sys::ComponentContext::Create();
  REP(Init(component_context.get()));

  auto process_config = ProcessConfigLoader::LoadProcessConfig(kProcessConfigPath);
  if (!process_config) {
    FX_LOGS(INFO) << "No audio_core_config.json; using default configuration";
    auto default_config = ProcessConfig::Builder()
                              .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(
                                  VolumeCurve::kDefaultGainForMinVolume))
                              .Build();
    process_config = {std::move(default_config)};
  }
  FX_CHECK(process_config);
  auto config_handle = ProcessConfig::set_instance(*process_config);

  auto context = Context::Create(std::move(threading_model), std::move(component_context),
                                 PlugDetector::Create(), std::move(*process_config));
  context->PublishOutgoingServices();

  AudioCoreImpl audio_core(context.get());
  auto thermal_agent = ThermalAgent::CreateAndServe(context.get());

  ProfileProvider profile_provider(context->component_context());
  context->component_context().outgoing()->AddPublicService(
      profile_provider.GetFidlRequestHandler());

  context->threading_model().RunAndJoinAllThreads();
  return 0;
}
