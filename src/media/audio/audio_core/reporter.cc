// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/media_metrics_registry.cb.h"

using namespace audio_output_underflow_duration_metric_dimension_time_since_boot_scope;

namespace media::audio {

#if ENABLE_REPORTER

constexpr char kRendererNotFound[] = "Specified renderer not found";
constexpr char kCapturerNotFound[] = "Specified capturer not found";
constexpr char kPayloadNotFound[] = "Specified payload buffer not found";
constexpr char kDeviceNotFound[] = "Specified device not found";

// static
Reporter& Reporter::Singleton() {
  static Reporter singleton;
  return singleton;
}

void Reporter::Init(sys::ComponentContext* component_context) {
  FX_DCHECK(component_context);
  FX_DCHECK(!component_context_);
  component_context_ = component_context;

  InitInspect();
  InitCobalt();
}

void Reporter::InitInspect() {
  inspector_ = std::make_shared<sys::ComponentInspector>(component_context_);
  inspect::Node& root_node = inspector_->root();
  failed_to_open_device_count_ = root_node.CreateUint("count of failures to open device", 0);
  failed_to_obtain_fdio_service_channel_count_ =
      root_node.CreateUint("count of failures to obtain device fdio service channel", 0);
  failed_to_obtain_stream_channel_count_ =
      root_node.CreateUint("count of failures to obtain device stream channel", 0);
  device_startup_failed_count_ = root_node.CreateUint("count of failures to start a device", 0);

  outputs_node_ = root_node.CreateChild("output devices");
  inputs_node_ = root_node.CreateChild("input devices");
  renderers_node_ = root_node.CreateChild("renderers");
  capturers_node_ = root_node.CreateChild("capturers");
}

void Reporter::InitCobalt() {
  component_context_->svc()->Connect(cobalt_factory_.NewRequest());
  if (!cobalt_factory_) {
    FX_LOGS(ERROR) << "audio_core could not connect to cobalt. No metrics will be captured.";
    return;
  }

  cobalt_factory_->CreateLoggerFromProjectName(
      "media", fuchsia::cobalt::ReleaseStage::GA, cobalt_logger_.NewRequest(),
      [this](fuchsia::cobalt::Status status) {
        if (status != fuchsia::cobalt::Status::OK) {
          FX_PLOGS(ERROR, fidl::ToUnderlying(status))
              << "audio_core could not create Cobalt logger";
          cobalt_logger_ = nullptr;
        }
      });
}

void Reporter::FailedToOpenDevice(const std::string& name, bool is_input, int err) {
  failed_to_open_device_count_.Add(1);
}

void Reporter::FailedToObtainFdioServiceChannel(const std::string& name, bool is_input,
                                                zx_status_t status) {
  failed_to_obtain_fdio_service_channel_count_.Add(1);
}

void Reporter::FailedToObtainStreamChannel(const std::string& name, bool is_input,
                                           zx_status_t status) {
  failed_to_obtain_stream_channel_count_.Add(1);
}

void Reporter::AddingDevice(const std::string& name, const AudioDevice& device) {
  if (device.is_output()) {
    outputs_.emplace(&device, Output(outputs_node_.CreateChild(name)));
  } else {
    FX_DCHECK(device.is_input());
    inputs_.emplace(&device, Input(inputs_node_.CreateChild(name)));
  }
}

void Reporter::RemovingDevice(const AudioDevice& device) {
  if (device.is_output()) {
    outputs_.erase(&device);
  } else {
    FX_DCHECK(device.is_input());
    inputs_.erase(&device);
  }
}

void Reporter::DeviceStartupFailed(const AudioDevice& device) {
  device_startup_failed_count_.Add(1);
}

void Reporter::IgnoringDevice(const AudioDevice& device) {
  // Not reporting this via inspect.
}

void Reporter::ActivatingDevice(const AudioDevice& device) {
  // Not reporting this via inspect...devices not activated are quickly removed.
}

void Reporter::SettingDeviceGainInfo(const AudioDevice& device,
                                     const fuchsia::media::AudioGainInfo& gain_info,
                                     uint32_t set_flags) {
  Device* d = device.is_output() ? FindOutput(device) : FindInput(device);
  if (d == nullptr) {
    FX_LOGS(ERROR) << kDeviceNotFound;
    return;
  }

  if (set_flags & fuchsia::media::SetAudioGainFlag_GainValid) {
    d->gain_db_.Set(gain_info.gain_db);
  }

  if (set_flags & fuchsia::media::SetAudioGainFlag_MuteValid) {
    d->muted_.Set((gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute) ? 1 : 0);
  }

  if (set_flags & fuchsia::media::SetAudioGainFlag_AgcValid) {
    d->agc_supported_.Set((gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported) ? 1
                                                                                             : 0);
    d->agc_enabled_.Set((gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled) ? 1 : 0);
  }
}

void Reporter::AddingRenderer(const fuchsia::media::AudioRenderer& renderer) {
  renderers_.emplace(&renderer, Renderer(renderers_node_.CreateChild(NextRendererName())));
}

void Reporter::RemovingRenderer(const fuchsia::media::AudioRenderer& renderer) {
  renderers_.erase(&renderer);
}

void Reporter::SettingRendererStreamType(const fuchsia::media::AudioRenderer& renderer,
                                         const fuchsia::media::AudioStreamType& stream_type) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }

  r->sample_format_.Set(static_cast<uint64_t>(stream_type.sample_format));
  r->channels_.Set(stream_type.channels);
  r->frames_per_second_.Set(stream_type.frames_per_second);
}

void Reporter::AddingRendererPayloadBuffer(const fuchsia::media::AudioRenderer& renderer,
                                           uint32_t buffer_id, uint64_t size) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }

  r->payload_buffers_.emplace(
      buffer_id,
      PayloadBuffer(r->payload_buffers_node_.CreateChild(std::to_string(buffer_id)), size));
}

void Reporter::RemovingRendererPayloadBuffer(const fuchsia::media::AudioRenderer& renderer,
                                             uint32_t buffer_id) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }
  r->payload_buffers_.erase(buffer_id);
}

void Reporter::SendingRendererPacket(const fuchsia::media::AudioRenderer& renderer,
                                     const fuchsia::media::StreamPacket& packet) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }
  auto payload_buffer = r->payload_buffers_.find(packet.payload_buffer_id);
  if (payload_buffer == r->payload_buffers_.end()) {
    FX_LOGS(ERROR) << kPayloadNotFound;
    return;
  }
  payload_buffer->second.packets_.Add(1);
}

void Reporter::SettingRendererGain(const fuchsia::media::AudioRenderer& renderer, float gain_db) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }

  r->gain_db_.Set(gain_db);
}

void Reporter::SettingRendererGainWithRamp(const fuchsia::media::AudioRenderer& renderer,
                                           float gain_db, zx::duration duration,
                                           fuchsia::media::audio::RampType ramp_type) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }

  // Just counting these for now.
  r->set_gain_with_ramp_calls_.Add(1);
}

void Reporter::SettingRendererMute(const fuchsia::media::AudioRenderer& renderer, bool muted) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }

  r->muted_.Set(muted);
}

void Reporter::SettingRendererMinLeadTime(const fuchsia::media::AudioRenderer& renderer,
                                          zx::duration min_lead_time) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }

  r->min_lead_time_ns_.Set(static_cast<uint64_t>(min_lead_time.to_nsecs()));
}

void Reporter::SettingRendererPtsContinuityThreshold(const fuchsia::media::AudioRenderer& renderer,
                                                     float threshold_seconds) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FX_LOGS(ERROR) << kRendererNotFound;
    return;
  }

  r->pts_continuity_threshold_seconds_.Set(threshold_seconds);
}

void Reporter::AddingCapturer(const fuchsia::media::AudioCapturer& capturer) {
  capturers_.emplace(&capturer, Capturer(capturers_node_.CreateChild(NextCapturerName())));
}

void Reporter::RemovingCapturer(const fuchsia::media::AudioCapturer& capturer) {
  capturers_.erase(&capturer);
}

void Reporter::SettingCapturerStreamType(const fuchsia::media::AudioCapturer& capturer,
                                         const fuchsia::media::AudioStreamType& stream_type) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FX_LOGS(ERROR) << kCapturerNotFound;
    return;
  }

  c->sample_format_.Set(static_cast<uint64_t>(stream_type.sample_format));
  c->channels_.Set(stream_type.channels);
  c->frames_per_second_.Set(stream_type.frames_per_second);
}

void Reporter::AddingCapturerPayloadBuffer(const fuchsia::media::AudioCapturer& capturer,
                                           uint32_t buffer_id, uint64_t size) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FX_LOGS(ERROR) << kCapturerNotFound;
    return;
  }

  c->payload_buffers_.emplace(
      buffer_id,
      PayloadBuffer(c->payload_buffers_node_.CreateChild(std::to_string(buffer_id)), size));
}

void Reporter::SendingCapturerPacket(const fuchsia::media::AudioCapturer& capturer,
                                     const fuchsia::media::StreamPacket& packet) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FX_LOGS(ERROR) << kCapturerNotFound;
    return;
  }
  auto payload_buffer = c->payload_buffers_.find(packet.payload_buffer_id);
  if (payload_buffer == c->payload_buffers_.end()) {
    FX_LOGS(ERROR) << kPayloadNotFound;
    return;
  }
  payload_buffer->second.packets_.Add(1);
}

void Reporter::SettingCapturerGain(const fuchsia::media::AudioCapturer& capturer, float gain_db) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FX_LOGS(ERROR) << kCapturerNotFound;
    return;
  }

  c->gain_db_.Set(gain_db);
}

void Reporter::SettingCapturerGainWithRamp(const fuchsia::media::AudioCapturer& capturer,
                                           float gain_db, zx::duration duration,
                                           fuchsia::media::audio::RampType ramp_type) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FX_LOGS(ERROR) << kCapturerNotFound;
    return;
  }

  // Just counting these for now.
  c->set_gain_with_ramp_calls_.Add(1);
}

void Reporter::SettingCapturerMute(const fuchsia::media::AudioCapturer& capturer, bool muted) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FX_LOGS(ERROR) << kCapturerNotFound;
    return;
  }

  c->muted_.Set(muted);
}

Reporter::Device* Reporter::FindOutput(const AudioDevice& device) {
  auto i = outputs_.find(&device);
  return i == outputs_.end() ? nullptr : &i->second;
}

Reporter::Device* Reporter::FindInput(const AudioDevice& device) {
  auto i = inputs_.find(&device);
  return i == inputs_.end() ? nullptr : &i->second;
}

Reporter::Renderer* Reporter::FindRenderer(const fuchsia::media::AudioRenderer& renderer) {
  auto i = renderers_.find(&renderer);
  return i == renderers_.end() ? nullptr : &i->second;
}

Reporter::Capturer* Reporter::FindCapturer(const fuchsia::media::AudioCapturer& capturer) {
  auto i = capturers_.find(&capturer);
  return i == capturers_.end() ? nullptr : &i->second;
}

std::string Reporter::NextRendererName() {
  std::ostringstream os;
  os << ++next_renderer_name_;
  return os.str();
}

std::string Reporter::NextCapturerName() {
  std::ostringstream os;
  os << ++next_capturer_name_;
  return os.str();
}

void Reporter::OutputUnderflow(zx::duration output_underflow_duration,
                               zx::time uptime_to_underflow) {
  // Bucket this into exponentially-increasing time since system boot.
  // By default, bucket the overflow into the last bucket

  int bucket = UpMoreThan64m;
  zx::duration uptime = uptime_to_underflow - zx::time(0);

  if (uptime < zx::sec(15)) {
    bucket = UpLessThan15s;
  } else if (uptime < zx::sec(30)) {
    bucket = UpLessThan30s;
  } else if (uptime < zx::min(1)) {
    bucket = UpLessThan1m;
  } else if (uptime < zx::min(2)) {
    bucket = UpLessThan2m;
  } else if (uptime < zx::min(4)) {
    bucket = UpLessThan4m;
  } else if (uptime < zx::min(8)) {
    bucket = UpLessThan8m;
  } else if (uptime < zx::min(16)) {
    bucket = UpLessThan16m;
  } else if (uptime < zx::min(32)) {
    bucket = UpLessThan32m;
  } else if (uptime < zx::min(64)) {
    bucket = UpLessThan64m;
  }

  if (!cobalt_logger_) {
    FX_LOGS(ERROR) << "UNDERFLOW: Failed to obtain the Cobalt logger";
    return;
  }

  cobalt_logger_->LogElapsedTime(
      kAudioOutputUnderflowDurationMetricId, bucket, "", output_underflow_duration.get(),
      [](fuchsia::cobalt::Status status) {
        if (status != fuchsia::cobalt::Status::OK &&
            status != fuchsia::cobalt::Status::BUFFER_FULL) {
          FX_PLOGS(ERROR, fidl::ToUnderlying(status)) << "Cobalt logger returned an error";
        }
      });
}

#endif  // ENABLE_REPORTER

}  // namespace media::audio
