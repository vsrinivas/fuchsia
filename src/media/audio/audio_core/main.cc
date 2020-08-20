// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/logger.h>
#include <lib/trace-provider/provider.h>

#include "src/lib/fxl/command_line.h"
#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/base_capturer.h"
#include "src/media/audio/audio_core/plug_detector.h"
#include "src/media/audio/audio_core/process_config_loader.h"
#include "src/media/audio/audio_core/profile_provider.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/audio_core/thermal_agent.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/ultrasound_factory.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

constexpr char kProcessConfigPath[] = "/config/data/audio_core_config.json";

static int StartAudioCore() {
  auto threading_model = ThreadingModel::CreateWithMixStrategy(MixStrategy::kThreadPerMix);
  trace::TraceProviderWithFdio trace_provider(threading_model->FidlDomain().dispatcher());

  syslog::SetLogSettings({.min_log_level = FX_LOG_INFO}, {"audio_core"});

  FX_LOGS(INFO) << "AudioCore starting up";

  // Initialize our telemetry reporter (which optimizes to nothing if ENABLE_REPORTER is set to 0).
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  Reporter::InitializeSingleton(*component_context, *threading_model);

  auto process_config = ProcessConfigLoader::LoadProcessConfig(kProcessConfigPath);
  if (process_config.is_error()) {
    FX_LOGS(WARNING) << "Failed to load audio_core_config.json;" << process_config.error()
                     << ". Falling back to default configuration.";
    process_config = fit::ok(ProcessConfig::Builder()
                                 .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(
                                     VolumeCurve::kDefaultGainForMinVolume))
                                 .Build());
  }
  FX_CHECK(process_config);
  auto config_handle = ProcessConfig::set_instance(process_config.value());

  auto context = Context::Create(std::move(threading_model), std::move(component_context),
                                 PlugDetector::Create(), process_config.take_value());
  context->PublishOutgoingServices();

  AudioCoreImpl audio_core(context.get());
  auto thermal_agent = ThermalAgent::CreateAndServe(context.get());
  auto ultrasound_factory = UltrasoundFactory::CreateAndServe(context.get());

  ProfileProvider profile_provider(context->component_context());
  context->component_context().outgoing()->AddPublicService(
      profile_provider.GetFidlRequestHandler());

  context->threading_model().RunAndJoinAllThreads();
  return 0;
}

}  // namespace media::audio

int main(int argc, const char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  media::audio::BaseCapturer::SetMustReleasePackets(cl.HasOption("captures-must-release-packets"));
  media::audio::StartAudioCore();
}
