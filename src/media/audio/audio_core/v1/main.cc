// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "src/lib/fxl/command_line.h"
#include "src/media/audio/audio_core/shared/pin_executable_memory.h"
#include "src/media/audio/audio_core/shared/process_config_loader.h"
#include "src/media/audio/audio_core/v1/audio_core_impl.h"
#include "src/media/audio/audio_core/v1/base_capturer.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/plug_detector.h"
#include "src/media/audio/audio_core/v1/profile_provider.h"
#include "src/media/audio/audio_core/v1/reporter.h"
#include "src/media/audio/audio_core/v1/thermal_watcher.h"
#include "src/media/audio/audio_core/v1/threading_model.h"
#include "src/media/audio/audio_core/v1/ultrasound_factory.h"

namespace media::audio {

constexpr char kProcessConfigPath[] = "/config/data/audio_core_config.json";

static int StartAudioCore(const fxl::CommandLine& cl) {
  syslog::SetLogSettings({.min_log_level = syslog::LOG_INFO}, {"audio_core"});

  FX_LOGS(INFO) << "AudioCore starting up";

  // Page in and pin our executable.
  PinExecutableMemory::Singleton();

  auto process_config = ProcessConfigLoader::LoadProcessConfig(kProcessConfigPath);
  if (process_config.is_error()) {
    FX_LOGS(WARNING) << "Failed to load " << kProcessConfigPath << ": " << process_config.error()
                     << ". Falling back to default configuration.";
    process_config = fpromise::ok(ProcessConfig::Builder()
                                      .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(
                                          VolumeCurve::kDefaultGainForMinVolume))
                                      .Build());
  }
  FX_CHECK(process_config);
  auto threading_model = ThreadingModel::CreateWithMixStrategy(
      MixStrategy::kThreadPerMix, process_config.value().mix_profile_config());
  trace::TraceProviderWithFdio trace_provider(threading_model->FidlDomain().dispatcher());

  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  auto enable_cobalt = !cl.HasOption("disable-cobalt");
  Reporter::InitializeSingleton(*component_context, *threading_model, enable_cobalt);

  auto context = Context::Create(std::move(threading_model), std::move(component_context),
                                 PlugDetector::Create(), process_config.take_value(),
                                 std::make_shared<AudioCoreClockFactory>());
  context->PublishOutgoingServices();

  auto thermal_watcher = ThermalWatcher::CreateAndWatch(*context);
  auto ultrasound_factory = UltrasoundFactory::CreateAndServe(context.get());

  ProfileProvider profile_provider(context->component_context(),
                                   context->process_config().mix_profile_config());
  context->component_context().outgoing()->AddPublicService(
      profile_provider.GetFidlRequestHandler());

  context->threading_model().RunAndJoinAllThreads();
  return 0;
}

}  // namespace media::audio

int main(int argc, const char** argv) {
  media::audio::StartAudioCore(fxl::CommandLineFromArgcArgv(argc, argv));
}
