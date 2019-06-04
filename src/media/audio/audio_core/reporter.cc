// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/audio_device.h"

namespace media::audio {

#if ENABLE_REPORTER

// static
Reporter& Reporter::Singleton() {
  static Reporter singleton;
  return singleton;
}

void Reporter::Init(sys::ComponentContext* component_context) {
  FXL_DCHECK(component_context);
  FXL_DCHECK(!component_context_);
  component_context_ = component_context;

  inspector_ = inspect::ComponentInspector::Initialize(component_context_);
  inspect::Node& root_node = inspector_->root_tree()->GetRoot();
  failed_to_open_device_count_ =
      root_node.CreateUIntMetric("count of failures to open device", 0);
  failed_to_obtain_fdio_service_channel_count_ = root_node.CreateUIntMetric(
      "count of failures to obtain device fdio service channel", 0);
  failed_to_obtain_stream_channel_count_ = root_node.CreateUIntMetric(
      "count of failures to obtain device stream channel", 0);
  device_startup_failed_count_ =
      root_node.CreateUIntMetric("count of failures to start a device", 0);

  outputs_node_ = root_node.CreateChild("output devices");
  inputs_node_ = root_node.CreateChild("input devices");
  renderers_node_ = root_node.CreateChild("renderers");
  capturers_node_ = root_node.CreateChild("capturers");
}

void Reporter::FailedToOpenDevice(const std::string& name, bool is_input,
                                  int err) {
  failed_to_open_device_count_.Add(1);
}

void Reporter::FailedToObtainFdioServiceChannel(const std::string& name,
                                                bool is_input,
                                                zx_status_t status) {
  failed_to_obtain_fdio_service_channel_count_.Add(1);
}

void Reporter::FailedToObtainStreamChannel(const std::string& name,
                                           bool is_input, zx_status_t status) {
  failed_to_obtain_stream_channel_count_.Add(1);
}

void Reporter::AddingDevice(const std::string& name,
                            const AudioDevice& device) {
  if (device.is_output()) {
    outputs_.emplace(&device, Output(outputs_node_.CreateChild(name)));
  } else {
    FXL_DCHECK(device.is_input());
    inputs_.emplace(&device, Input(inputs_node_.CreateChild(name)));
  }
}

void Reporter::RemovingDevice(const AudioDevice& device) {
  if (device.is_output()) {
    outputs_.erase(&device);
  } else {
    FXL_DCHECK(device.is_input());
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

void Reporter::SettingDeviceGainInfo(
    const AudioDevice& device, const fuchsia::media::AudioGainInfo& gain_info,
    uint32_t set_flags) {
  Device* d = device.is_output() ? FindOutput(device) : FindInput(device);
  if (d == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  if (set_flags & fuchsia::media::SetAudioGainFlag_GainValid) {
    d->gain_db_.Set(gain_info.gain_db);
  }

  if (set_flags & fuchsia::media::SetAudioGainFlag_MuteValid) {
    d->muted_.Set(
        (gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute) ? 1 : 0);
  }

  if (set_flags & fuchsia::media::SetAudioGainFlag_AgcValid) {
    d->agc_supported_.Set(
        (gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported) ? 1
                                                                           : 0);
    d->agc_enabled_.Set(
        (gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled) ? 1
                                                                         : 0);
  }
}

void Reporter::AddingRenderer(const AudioRendererImpl& renderer) {
  renderers_.emplace(&renderer,
                     Renderer(renderers_node_.CreateChild(NextRendererName())));
}

void Reporter::RemovingRenderer(const AudioRendererImpl& renderer) {
  renderers_.erase(&renderer);
}

void Reporter::SettingRendererStreamType(
    const AudioRendererImpl& renderer,
    const fuchsia::media::AudioStreamType& stream_type) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  r->sample_format_.Set(static_cast<uint64_t>(stream_type.sample_format));
  r->channels_.Set(stream_type.channels);
  r->frames_per_second_.Set(stream_type.frames_per_second);
}

void Reporter::AddingRendererPayloadBuffer(const AudioRendererImpl& renderer,
                                           uint32_t buffer_id, uint64_t size) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  // TODO(dalesat): Expand when multiple buffers are allowed.
  r->payload_buffer_size_.Set(size);
}

void Reporter::SettingRendererGain(const AudioRendererImpl& renderer,
                                   float gain_db) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  r->gain_db_.Set(gain_db);
}

void Reporter::SettingRendererGainWithRamp(
    const AudioRendererImpl& renderer, float gain_db, zx_duration_t duration_ns,
    fuchsia::media::audio::RampType ramp_type) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  // Just counting these for now.
  r->set_gain_with_ramp_calls_.Add(1);
}

void Reporter::SettingRendererMute(const AudioRendererImpl& renderer,
                                   bool muted) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  r->muted_.Set(muted);
}

void Reporter::SettingRendererMinClockLeadTime(
    const AudioRendererImpl& renderer, int64_t min_clock_lead_time_ns) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  r->min_clock_lead_time_ns_.Set(static_cast<uint64_t>(min_clock_lead_time_ns));
}

void Reporter::SettingRendererPtsContinuityThreshold(
    const AudioRendererImpl& renderer, float threshold_seconds) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  r->pts_continuity_threshold_seconds_.Set(threshold_seconds);
}

void Reporter::AddingCapturer(const AudioCapturerImpl& capturer) {
  capturers_.emplace(&capturer,
                     Capturer(capturers_node_.CreateChild(NextCapturerName())));
}

void Reporter::RemovingCapturer(const AudioCapturerImpl& capturer) {
  capturers_.erase(&capturer);
}

void Reporter::SettingCapturerStreamType(
    const AudioCapturerImpl& capturer,
    const fuchsia::media::AudioStreamType& stream_type) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  c->sample_format_.Set(static_cast<uint64_t>(stream_type.sample_format));
  c->channels_.Set(stream_type.channels);
  c->frames_per_second_.Set(stream_type.frames_per_second);
}

void Reporter::AddingCapturerPayloadBuffer(const AudioCapturerImpl& capturer,
                                           uint32_t buffer_id, uint64_t size) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  // TODO(dalesat): Expand when multiple buffers are allowed.
  c->payload_buffer_size_.Set(size);
}

void Reporter::SettingCapturerGain(const AudioCapturerImpl& capturer,
                                   float gain_db) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  c->gain_db_.Set(gain_db);
}

void Reporter::SettingCapturerGainWithRamp(
    const AudioCapturerImpl& capturer, float gain_db, zx_duration_t duration_ns,
    fuchsia::media::audio::RampType ramp_type) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FXL_DLOG(FATAL);
    return;
  }

  // Just counting these for now.
  c->set_gain_with_ramp_calls_.Add(1);
}

void Reporter::SettingCapturerMute(const AudioCapturerImpl& capturer,
                                   bool muted) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    FXL_DLOG(FATAL);
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

Reporter::Renderer* Reporter::FindRenderer(const AudioRendererImpl& renderer) {
  auto i = renderers_.find(&renderer);
  return i == renderers_.end() ? nullptr : &i->second;
}

Reporter::Capturer* Reporter::FindCapturer(const AudioCapturerImpl& capturer) {
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

#endif  // ENABLE_REPORTER

}  // namespace media::audio
