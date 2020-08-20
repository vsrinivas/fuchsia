// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/media_metrics_registry.cb.h"

namespace media::audio {

#if ENABLE_REPORTER

namespace {
static Reporter* singleton;
constexpr char kPayloadNotFound[] = "Specified payload buffer not found";
}  // namespace

// static
Reporter* Reporter::Singleton() { return singleton; }

// static
void Reporter::CreateSingleton(sys::ComponentContext& component_context,
                               ThreadingModel& threading_model) {
  FX_CHECK(!singleton);
  singleton = new Reporter(component_context, threading_model);
}

Reporter::Reporter(sys::ComponentContext& component_context, ThreadingModel& threading_model)
    : component_context_(component_context), threading_model_(threading_model) {
  InitInspect();
  InitCobalt();
}

void Reporter::InitInspect() {
  inspector_ = std::make_unique<sys::ComponentInspector>(&component_context_);
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
  component_context_.svc()->Connect(cobalt_factory_.NewRequest());
  if (!cobalt_factory_) {
    FX_LOGS(ERROR) << "audio_core could not connect to cobalt. No metrics will be captured.";
    return;
  }

  cobalt_factory_->CreateLoggerFromProjectId(kProjectId, cobalt_logger_.NewRequest(),
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
    outputs_.emplace(&device, Output(outputs_node_.CreateChild(name), *this));
  } else {
    FX_DCHECK(device.is_input());
    inputs_.emplace(&device, inputs_node_.CreateChild(name));
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
                                     fuchsia::media::AudioGainValidFlags set_flags) {
  Device* d = device.is_output() ? static_cast<Device*>(FindOutput(device))
                                 : static_cast<Device*>(FindInput(device));
  if (!d) {
    return;
  }

  if ((set_flags & fuchsia::media::AudioGainValidFlags::GAIN_VALID) ==
      fuchsia::media::AudioGainValidFlags::GAIN_VALID) {
    d->gain_db_.Set(gain_info.gain_db);
  }

  if ((set_flags & fuchsia::media::AudioGainValidFlags::MUTE_VALID) ==
      fuchsia::media::AudioGainValidFlags::MUTE_VALID) {
    d->muted_.Set(((gain_info.flags & fuchsia::media::AudioGainInfoFlags::MUTE) ==
                   fuchsia::media::AudioGainInfoFlags::MUTE)
                      ? 1
                      : 0);
  }

  if ((set_flags & fuchsia::media::AudioGainValidFlags::AGC_VALID) ==
      fuchsia::media::AudioGainValidFlags::AGC_VALID) {
    d->agc_supported_.Set(((gain_info.flags & fuchsia::media::AudioGainInfoFlags::AGC_SUPPORTED) ==
                           fuchsia::media::AudioGainInfoFlags::AGC_SUPPORTED)
                              ? 1
                              : 0);
    d->agc_enabled_.Set(((gain_info.flags & fuchsia::media::AudioGainInfoFlags::AGC_ENABLED) ==
                         fuchsia::media::AudioGainInfoFlags::AGC_ENABLED)
                            ? 1
                            : 0);
  }
}

void Reporter::AddingRenderer(const fuchsia::media::AudioRenderer& renderer) {
  renderers_.emplace(&renderer, Renderer(renderers_node_.CreateChild(NextRendererName()), *this));
}

void Reporter::RemovingRenderer(const fuchsia::media::AudioRenderer& renderer) {
  renderers_.erase(&renderer);
}

void Reporter::SettingRendererStreamType(const fuchsia::media::AudioRenderer& renderer,
                                         const fuchsia::media::AudioStreamType& stream_type) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
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
    return;
  }
  r->payload_buffers_.erase(buffer_id);
}

void Reporter::SendingRendererPacket(const fuchsia::media::AudioRenderer& renderer,
                                     const fuchsia::media::StreamPacket& packet) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
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
    return;
  }

  r->gain_db_.Set(gain_db);
}

void Reporter::SettingRendererGainWithRamp(const fuchsia::media::AudioRenderer& renderer,
                                           float gain_db, zx::duration duration,
                                           fuchsia::media::audio::RampType ramp_type) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    return;
  }

  // Just counting these for now.
  r->set_gain_with_ramp_calls_.Add(1);
}

void Reporter::SettingRendererFinalGain(const fuchsia::media::AudioRenderer& renderer,
                                        float gain_db) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    return;
  }

  r->final_stream_gain_.Set(gain_db);
}

void Reporter::SettingRendererMute(const fuchsia::media::AudioRenderer& renderer, bool muted) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    return;
  }

  r->muted_.Set(muted);
}

void Reporter::SettingRendererMinLeadTime(const fuchsia::media::AudioRenderer& renderer,
                                          zx::duration min_lead_time) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    return;
  }

  r->min_lead_time_ns_.Set(static_cast<uint64_t>(min_lead_time.to_nsecs()));
}

void Reporter::SettingRendererPtsContinuityThreshold(const fuchsia::media::AudioRenderer& renderer,
                                                     float threshold_seconds) {
  Renderer* r = FindRenderer(renderer);
  if (r == nullptr) {
    return;
  }

  r->pts_continuity_threshold_seconds_.Set(threshold_seconds);
}

void Reporter::AddingCapturer(const fuchsia::media::AudioCapturer& capturer) {
  capturers_.emplace(&capturer, Capturer(capturers_node_.CreateChild(NextCapturerName()), *this));
}

void Reporter::RemovingCapturer(const fuchsia::media::AudioCapturer& capturer) {
  capturers_.erase(&capturer);
}

void Reporter::SettingCapturerStreamType(const fuchsia::media::AudioCapturer& capturer,
                                         const fuchsia::media::AudioStreamType& stream_type) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
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
    return;
  }

  c->gain_db_.Set(gain_db);
}

void Reporter::SettingCapturerGainWithRamp(const fuchsia::media::AudioCapturer& capturer,
                                           float gain_db, zx::duration duration,
                                           fuchsia::media::audio::RampType ramp_type) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    return;
  }

  // Just counting these for now.
  c->set_gain_with_ramp_calls_.Add(1);
}

void Reporter::SettingCapturerMute(const fuchsia::media::AudioCapturer& capturer, bool muted) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    return;
  }

  c->muted_.Set(muted);
}

void Reporter::SettingCapturerMinFenceTime(const fuchsia::media::AudioCapturer& capturer,
                                           zx::duration min_fence_time) {
  Capturer* c = FindCapturer(capturer);
  if (c == nullptr) {
    return;
  }

  c->min_fence_time_ns_.Set(static_cast<uint64_t>(min_fence_time.to_nsecs()));
}

void Reporter::OutputDeviceStartSession(const AudioDevice& device, zx::time start_time) {
  auto o = FindOutput(device);
  if (o) {
    o->device_underflows_->StartSession(start_time);
  }
}

void Reporter::OutputDeviceStopSession(const AudioDevice& device, zx::time stop_time) {
  auto o = FindOutput(device);
  if (o) {
    o->device_underflows_->StopSession(stop_time);
  }
}

void Reporter::OutputDeviceUnderflow(const AudioDevice& device, zx::time start_time,
                                     zx::time stop_time) {
  auto o = FindOutput(device);
  if (o) {
    o->device_underflows_->Report(start_time, stop_time);
  }
}

void Reporter::OutputPipelineStartSession(const AudioDevice& device, zx::time start_time) {
  auto o = FindOutput(device);
  if (o) {
    o->pipeline_underflows_->StartSession(start_time);
  }
}

void Reporter::OutputPipelineStopSession(const AudioDevice& device, zx::time stop_time) {
  auto o = FindOutput(device);
  if (o) {
    o->pipeline_underflows_->StopSession(stop_time);
  }
}

void Reporter::OutputPipelineUnderflow(const AudioDevice& device, zx::time start_time,
                                       zx::time stop_time) {
  auto o = FindOutput(device);
  if (o) {
    o->pipeline_underflows_->Report(start_time, stop_time);
  }
}

void Reporter::RendererStartSession(const fuchsia::media::AudioRenderer& renderer,
                                    zx::time start_time) {
  auto r = FindRenderer(renderer);
  if (r) {
    r->underflows_->StartSession(start_time);
  }
}

void Reporter::RendererStopSession(const fuchsia::media::AudioRenderer& renderer,
                                   zx::time stop_time) {
  auto r = FindRenderer(renderer);
  if (r) {
    r->underflows_->StopSession(stop_time);
  }
}

void Reporter::RendererUnderflow(const fuchsia::media::AudioRenderer& renderer, zx::time start_time,
                                 zx::time stop_time) {
  auto r = FindRenderer(renderer);
  if (r) {
    r->underflows_->Report(start_time, stop_time);
  }
}

void Reporter::CapturerStartSession(const fuchsia::media::AudioCapturer& capturer,
                                    zx::time start_time) {
  auto c = FindCapturer(capturer);
  if (c) {
    c->overflows_->StartSession(start_time);
  }
}

void Reporter::CapturerStopSession(const fuchsia::media::AudioCapturer& capturer,
                                   zx::time stop_time) {
  auto c = FindCapturer(capturer);
  if (c) {
    c->overflows_->StopSession(stop_time);
  }
}

void Reporter::CapturerOverflow(const fuchsia::media::AudioCapturer& capturer, zx::time start_time,
                                zx::time stop_time) {
  auto c = FindCapturer(capturer);
  if (c) {
    c->overflows_->Report(start_time, stop_time);
  }
}

Reporter::Output* Reporter::FindOutput(const AudioDevice& device) {
  auto i = outputs_.find(&device);
  if (i == outputs_.end()) {
    FX_LOGS(ERROR) << "Specified output device not found";
    return nullptr;
  }
  return &i->second;
}

Reporter::Input* Reporter::FindInput(const AudioDevice& device) {
  auto i = inputs_.find(&device);
  if (i == inputs_.end()) {
    FX_LOGS(ERROR) << "Specified input device not found";
    return nullptr;
  }
  return &i->second;
}

Reporter::Renderer* Reporter::FindRenderer(const fuchsia::media::AudioRenderer& renderer) {
  auto i = renderers_.find(&renderer);
  if (i == renderers_.end()) {
    FX_LOGS(ERROR) << "Specified renderer not found";
    return nullptr;
  }
  return &i->second;
}

Reporter::Capturer* Reporter::FindCapturer(const fuchsia::media::AudioCapturer& capturer) {
  auto i = capturers_.find(&capturer);
  if (i == capturers_.end()) {
    FX_LOGS(ERROR) << "Specified capturer not found";
    return nullptr;
  }
  return &i->second;
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

Reporter::Device::Device(inspect::Node node) : node_(std::move(node)) {
  gain_db_ = node_.CreateDouble("gain db", 0.0);
  muted_ = node_.CreateUint("muted", 0);
  agc_supported_ = node_.CreateUint("agc supported", 0);
  agc_enabled_ = node_.CreateUint("agc enabled", 0);
}

Reporter::Output::Output(inspect::Node node, Reporter& reporter)
    : Device(std::move(node)),
      device_underflows_(std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
          .event_name = "device underflows",
          .parent_node = node_,
          .reporter = reporter,
          .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::OutputDevice,
          .cobalt_event_duration_metric_id = kAudioUnderflowDurationMetricId,
          .cobalt_time_since_last_event_or_session_start_metric_id =
              kAudioTimeSinceLastUnderflowOrSessionStartMetricId,
      })),
      pipeline_underflows_(
          std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
              .event_name = "pipeline underflows",
              .parent_node = node_,
              .reporter = reporter,
              .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::OutputPipeline,
              .cobalt_event_duration_metric_id = kAudioUnderflowDurationMetricId,
              .cobalt_time_since_last_event_or_session_start_metric_id =
                  kAudioTimeSinceLastUnderflowOrSessionStartMetricId,
          })) {}

Reporter::Input::Input(inspect::Node node) : Device(std::move(node)) {}

Reporter::ClientPort::ClientPort(inspect::Node node) : node_(std::move(node)) {
  sample_format_ = node_.CreateUint("sample format", 0);
  channels_ = node_.CreateUint("channels", 0);
  frames_per_second_ = node_.CreateUint("frames per second", 0);
  payload_buffers_node_ = node_.CreateChild("payload buffers");
  gain_db_ = node_.CreateDouble("gain db", 0.0);
  muted_ = node_.CreateUint("muted", 0);
  set_gain_with_ramp_calls_ = node_.CreateUint("calls to SetGainWithRamp", 0);
}

Reporter::Renderer::Renderer(inspect::Node node, Reporter& reporter)
    : ClientPort(std::move(node)),
      underflows_(std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
          .event_name = "underflows",
          .parent_node = node_,
          .reporter = reporter,
          .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::Renderer,
          .cobalt_event_duration_metric_id = kAudioUnderflowDurationMetricId,
          .cobalt_time_since_last_event_or_session_start_metric_id =
              kAudioTimeSinceLastUnderflowOrSessionStartMetricId,
      })) {
  min_lead_time_ns_ = node_.CreateUint("min lead time (ns)", 0);
  pts_continuity_threshold_seconds_ = node_.CreateDouble("pts continuity threshold (s)", 0.0);
  final_stream_gain_ = node_.CreateDouble("final stream gain (post-volume) dbfs", 0.0);
}

Reporter::Capturer::Capturer(inspect::Node node, Reporter& reporter)
    : ClientPort(std::move(node)),
      overflows_(std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
          .event_name = "overflows",
          .parent_node = node_,
          .reporter = reporter,
          .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::Capturer,
          .cobalt_event_duration_metric_id = kAudioOverflowDurationMetricId,
          .cobalt_time_since_last_event_or_session_start_metric_id =
              kAudioTimeSinceLastOverflowOrSessionStartMetricId,
      })) {
  min_fence_time_ns_ = node_.CreateUint("min fence time (ns)", 0);
}

namespace {
constexpr auto kEventSessionStart = 0;
constexpr auto kEventOverflowUnderflow = 1;
static_assert(AudioTimeSinceLastOverflowOrSessionStartMetricDimensionLastEvent::SessionStart == 0);
static_assert(AudioTimeSinceLastUnderflowOrSessionStartMetricDimensionLastEvent::SessionStart == 0);
static_assert(AudioTimeSinceLastOverflowOrSessionStartMetricDimensionLastEvent::Overflow == 1);
static_assert(AudioTimeSinceLastUnderflowOrSessionStartMetricDimensionLastEvent::Underflow == 1);
}  // namespace

Reporter::OverflowUnderflowTracker::OverflowUnderflowTracker(Args args)
    : state_(State::Stopped),
      reporter_(args.reporter),
      cobalt_component_id_(args.cobalt_component_id),
      cobalt_event_duration_metric_id_(args.cobalt_event_duration_metric_id),
      cobalt_time_since_last_event_or_session_start_metric_id_(
          args.cobalt_time_since_last_event_or_session_start_metric_id) {
  node_ = args.parent_node.CreateChild(args.event_name);
  event_count_ = node_.CreateUint("count", 0);
  event_duration_ = node_.CreateUint("duration (ns)", 0);
  session_count_ = node_.CreateUint("session count", 0);
  total_duration_ = node_.CreateLazyValues("@wrapper", [this] {
    inspect::Inspector i;
    i.GetRoot().CreateUint("total duration of all parent sessions (ns)",
                           ComputeDurationOfAllSessions().get(), &i);
    return fit::make_ok_promise(std::move(i));
  });
}

void Reporter::OverflowUnderflowTracker::StartSession(zx::time start_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::Started) {
    FX_LOGS(ERROR) << "StartSession called on session that is already started";
    return;
  }

  session_count_.Add(1);

  state_ = State::Started;
  last_event_time_ = start_time;
  session_start_time_ = start_time;
  session_real_start_time_ = start_time;
  restart_session_timer_.PostDelayed(reporter_.threading_model_.IoDomain().dispatcher(),
                                     kMaxSessionDuration);
}

void Reporter::OverflowUnderflowTracker::StopSession(zx::time stop_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::Stopped) {
    FX_LOGS(ERROR) << "StopSession called on session that is already stopped";
    return;
  }

  LogCobaltDuration(kAudioSessionDurationMetricId, {cobalt_component_id_},
                    stop_time - session_start_time_);
  LogCobaltDuration(cobalt_time_since_last_event_or_session_start_metric_id_,
                    {cobalt_component_id_, kEventSessionStart}, stop_time - last_event_time_);

  state_ = State::Stopped;
  past_sessions_duration_ += stop_time - session_real_start_time_;
  restart_session_timer_.Cancel();
}

void Reporter::OverflowUnderflowTracker::RestartSession() {
  auto stop_time = zx::clock::get_monotonic();

  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::Stopped) {
    return;  // must have been stopped concurrently
  }

  LogCobaltDuration(kAudioSessionDurationMetricId, {cobalt_component_id_},
                    stop_time - session_start_time_);
  LogCobaltDuration(cobalt_time_since_last_event_or_session_start_metric_id_,
                    {cobalt_component_id_, kEventSessionStart}, stop_time - last_event_time_);

  last_event_time_ = stop_time;
  session_start_time_ = stop_time;
  restart_session_timer_.PostDelayed(reporter_.threading_model_.IoDomain().dispatcher(),
                                     kMaxSessionDuration);
}

zx::duration Reporter::OverflowUnderflowTracker::ComputeDurationOfAllSessions() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto dt = past_sessions_duration_;
  if (state_ == State::Started) {
    dt += zx::clock::get_monotonic() - session_real_start_time_;
  }
  return dt;
}

void Reporter::OverflowUnderflowTracker::Report(zx::time start_time, zx::time end_time) {
  if (end_time < start_time) {
    FX_LOGS(ERROR) << "Reported overflow/underflow with negative duration: " << start_time.get()
                   << " to " << end_time.get();
  }

  std::lock_guard<std::mutex> lock(mutex_);

  auto event_duration = end_time - start_time;
  event_count_.Add(1);
  event_duration_.Add(event_duration.get());

  LogCobaltDuration(cobalt_event_duration_metric_id_, {cobalt_component_id_}, event_duration);

  if (state_ != State::Started) {
    // This can happen because reporting can race with session boundaries. For example:
    // If the mixer detects a renderer underflow as the client concurrently pauses the
    // renderer, the Report and StopSession calls will race.
    FX_LOGS_FIRST_N(INFO, 20) << "Overflow/Underflow event arrived when the session is stopped";
    return;
  }

  LogCobaltDuration(cobalt_time_since_last_event_or_session_start_metric_id_,
                    {cobalt_component_id_, kEventOverflowUnderflow}, start_time - last_event_time_);
  last_event_time_ = end_time;
}

void Reporter::OverflowUnderflowTracker::LogCobaltDuration(uint32_t metric_id,
                                                           std::vector<uint32_t> event_codes,
                                                           zx::duration d) {
  auto& logger = reporter_.cobalt_logger_;
  if (!logger) {
    return;
  }
  auto e = fuchsia::cobalt::CobaltEvent{
      .metric_id = metric_id,
      .event_codes = event_codes,
      .payload = fuchsia::cobalt::EventPayload::WithElapsedMicros(d.get()),
  };
  logger->LogCobaltEvent(std::move(e), [](fuchsia::cobalt::Status status) {
    if (status == fuchsia::cobalt::Status::OK) {
      return;
    }
    if (status == fuchsia::cobalt::Status::BUFFER_FULL) {
      FX_LOGS_FIRST_N(WARNING, 50) << "Cobalt logger failed with buffer full";
    } else {
      FX_LOGS(ERROR) << "Cobalt logger failed with code " << static_cast<int>(status);
    }
  });
}

#endif  // ENABLE_REPORTER

}  // namespace media::audio
