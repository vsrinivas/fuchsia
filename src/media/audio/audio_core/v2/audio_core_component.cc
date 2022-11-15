// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v2/audio_core_component.h"

#include "src/media/audio/audio_core/shared/pin_executable_memory.h"
#include "src/media/audio/audio_core/shared/policy_loader.h"
#include "src/media/audio/audio_core/shared/process_config_loader.h"
#include "src/media/audio/audio_core/shared/reporter.h"
#include "src/media/audio/audio_core/shared/volume_curve.h"

namespace media_audio {

namespace {

using ::media::audio::ActivityDispatcherImpl;
using ::media::audio::PinExecutableMemory;
using ::media::audio::PolicyLoader;
using ::media::audio::ProcessConfig;
using ::media::audio::ProcessConfigLoader;
using ::media::audio::ProfileProvider;
using ::media::audio::Reporter;
using ::media::audio::StreamVolumeManager;
using ::media::audio::UsageGainReporterImpl;
using ::media::audio::UsageReporterImpl;
using ::media::audio::VolumeCurve;

constexpr char kProcessConfigPath[] = "/config/data/audio_core_config.json";

ProcessConfig LoadProcessConfig() {
  auto result = ProcessConfigLoader::LoadProcessConfig(kProcessConfigPath);
  if (result.is_ok()) {
    return std::move(result.value());
  }

  FX_LOGS(WARNING) << "Failed to load " << kProcessConfigPath << ": " << result.error()
                   << ". Falling back to default configuration.";
  return ProcessConfig::Builder()
      .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
      .Build();
}

}  // namespace

AudioCoreComponent::AudioCoreComponent(sys::ComponentContext& component_context,
                                       async_dispatcher_t* fidl_dispatcher,
                                       async_dispatcher_t* io_dispatcher, bool enable_cobalt)
    :  // Load configs
      process_config_(LoadProcessConfig()),
      policy_config_(PolicyLoader::LoadPolicy()) {
  // Pin all memory pages backed by executable files.
  PinExecutableMemory::Singleton();

  // Initialize metrics reporting and tracing before creating any objects.
  Reporter::InitializeSingleton(component_context, fidl_dispatcher, io_dispatcher, enable_cobalt);
  Reporter::Singleton().SetNumThermalStates(process_config_.thermal_config().states().size());
  trace_provider_ = std::make_unique<trace::TraceProviderWithFdio>(io_dispatcher);

  // Create objects.
  stream_volume_manager_ = std::make_unique<StreamVolumeManager>(fidl_dispatcher);
  activity_dispatcher_ = std::make_unique<ActivityDispatcherImpl>();
  profile_provider_ =
      std::make_unique<ProfileProvider>(component_context, process_config_.mix_profile_config());
  usage_gain_reporter_ = std::make_unique<UsageGainReporterImpl>(
      empty_device_lister_, *stream_volume_manager_, process_config_);
  usage_reporter_ = std::unique_ptr<UsageReporterImpl>();

  // Publish services.
  auto& out = *component_context.outgoing();
  out.AddPublicService(activity_dispatcher_->GetFidlRequestHandler());  // f.m.ActivityReporter
  out.AddPublicService(profile_provider_->GetFidlRequestHandler());     // f.m.ProfileProvider
  out.AddPublicService(usage_gain_reporter_->GetFidlRequestHandler());  // f.m.UsageGainReporter
  out.AddPublicService(usage_reporter_->GetFidlRequestHandler());       // f.m.UsageReporter
}

}  // namespace media_audio
