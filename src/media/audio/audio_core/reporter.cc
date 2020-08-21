// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/media_metrics_registry.cb.h"

namespace media::audio {

////////////////////////////////////////////////////////////////////////////////
// Singletons

namespace {
static std::mutex singleton_mutex;
static Reporter singleton_nop;
static std::unique_ptr<Reporter> singleton_real;
}  // namespace

////////////////////////////////////////////////////////////////////////////////
// No-op implementations, used before the Reporter has been initialized

namespace {
class OutputDeviceNop : public Reporter::OutputDevice {
 public:
  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetDriverName(const std::string& driver_name) override {}
  void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                   fuchsia::media::AudioGainValidFlags set_flags) override {}
  void DeviceUnderflow(zx::time start_time, zx::time end_time) override {}
  void PipelineUnderflow(zx::time start_time, zx::time end_time) override {}
};

class InputDeviceNop : public Reporter::InputDevice {
 public:
  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetDriverName(const std::string& driver_name) override {}
  void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                   fuchsia::media::AudioGainValidFlags set_flags) override {}
};

class RendererNop : public Reporter::Renderer {
 public:
  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetUsage(RenderUsage usage) override {}
  void SetStreamType(const fuchsia::media::AudioStreamType& stream_type) override {}
  void SetGain(float gain_db) override {}
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type) override {}
  void SetFinalGain(float gain_db) override {}
  void SetMute(bool muted) override {}
  void SetMinLeadTime(zx::duration min_lead_time) override {}
  void SetPtsContinuityThreshold(float threshold_seconds) override {}

  void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) override {}
  void RemovePayloadBuffer(uint32_t buffer_id) override {}
  void SendPacket(const fuchsia::media::StreamPacket& packet) override {}
  void Underflow(zx::time start_time, zx::time end_time) override {}
};

class CapturerNop : public Reporter::Capturer {
 public:
  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetUsage(CaptureUsage usage) override {}
  void SetStreamType(const fuchsia::media::AudioStreamType& stream_type) override {}
  void SetGain(float gain_db) override {}
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type) override {}
  void SetMute(bool muted) override {}
  void SetMinFenceTime(zx::duration min_fence_time) override {}

  void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) override {}
  void SendPacket(const fuchsia::media::StreamPacket& packet) override {}
  void Overflow(zx::time start_time, zx::time end_time) override {}
};
}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Real implementations of reporting objects.

class Reporter::OverflowUnderflowTracker {
 public:
  // Trackers begin in a "stopped" state and must move to a "started" state
  // before metrics can be reported. The Start/Stop events are intended to
  // mirror higher-level Play/Pause or Record/Stop events. If a session is not
  // stopped explicitly, it's stopped automatically by our destructor.
  void StartSession(zx::time start_time);
  void StopSession(zx::time stop_time);

  // Report an event with the given start and end times.
  void Report(zx::time start_time, zx::time end_time);

  struct Args {
    uint32_t component;
    std::string event_name;
    inspect::Node& parent_node;
    Reporter::Impl& impl;
    bool is_underflow;
    uint32_t cobalt_component_id;
  };
  OverflowUnderflowTracker(Args args);
  ~OverflowUnderflowTracker();

 private:
  void RestartSession();
  zx::duration ComputeDurationOfAllSessions();
  void LogCobaltDuration(uint32_t metric_id, std::vector<uint32_t> event_codes, zx::duration d);

  std::mutex mutex_;

  enum class State { Stopped, Started };
  State state_ FXL_GUARDED_BY(mutex_);

  // Ideally we'd record final cobalt metrics when the component exits, however we can't
  // be notified of component exit until we've switched to Components v2. In the interim,
  // we automatically restart sessions every hour. Inspect metrics don't have this limitation
  // and can use the "real" session times.
  static constexpr auto kMaxSessionDuration = zx::hour(1);
  async::TaskClosureMethod<OverflowUnderflowTracker, &OverflowUnderflowTracker::RestartSession>
      restart_session_timer_ FXL_GUARDED_BY(mutex_){this};

  zx::time last_event_time_ FXL_GUARDED_BY(mutex_);             // for cobalt
  zx::time session_start_time_ FXL_GUARDED_BY(mutex_);          // for cobalt
  zx::time session_real_start_time_ FXL_GUARDED_BY(mutex_);     // for inspect
  zx::duration past_sessions_duration_ FXL_GUARDED_BY(mutex_);  // for inspect

  inspect::Node node_;
  inspect::UintProperty event_count_;
  inspect::UintProperty event_duration_;
  inspect::UintProperty session_count_;
  inspect::LazyNode total_duration_;

  Reporter::Impl& impl_;
  const uint32_t cobalt_component_id_;
  const uint32_t cobalt_event_duration_metric_id_;
  const uint32_t cobalt_time_since_last_event_or_session_start_metric_id_;
};

class DeviceGainInfo {
 public:
  DeviceGainInfo(inspect::Node& node)
      : gain_db_(node.CreateDouble("gain db", 0.0)),
        muted_(node.CreateBool("muted", false)),
        agc_supported_(node.CreateBool("agc supported", false)),
        agc_enabled_(node.CreateBool("agc enabled", false)) {}

  void Set(const fuchsia::media::AudioGainInfo& gain_info,
           fuchsia::media::AudioGainValidFlags set_flags) {
    if ((set_flags & fuchsia::media::AudioGainValidFlags::GAIN_VALID) ==
        fuchsia::media::AudioGainValidFlags::GAIN_VALID) {
      gain_db_.Set(gain_info.gain_db);
    }

    if ((set_flags & fuchsia::media::AudioGainValidFlags::MUTE_VALID) ==
        fuchsia::media::AudioGainValidFlags::MUTE_VALID) {
      muted_.Set((gain_info.flags & fuchsia::media::AudioGainInfoFlags::MUTE) ==
                 fuchsia::media::AudioGainInfoFlags::MUTE);
    }

    if ((set_flags & fuchsia::media::AudioGainValidFlags::AGC_VALID) ==
        fuchsia::media::AudioGainValidFlags::AGC_VALID) {
      agc_supported_.Set((gain_info.flags & fuchsia::media::AudioGainInfoFlags::AGC_SUPPORTED) ==
                         fuchsia::media::AudioGainInfoFlags::AGC_SUPPORTED);
      agc_enabled_.Set((gain_info.flags & fuchsia::media::AudioGainInfoFlags::AGC_ENABLED) ==
                       fuchsia::media::AudioGainInfoFlags::AGC_ENABLED);
    }
  }

 private:
  inspect::DoubleProperty gain_db_;
  inspect::BoolProperty muted_;
  inspect::BoolProperty agc_supported_;
  inspect::BoolProperty agc_enabled_;
};

class Reporter::OutputDeviceImpl : public Reporter::OutputDevice {
 public:
  OutputDeviceImpl(Reporter::Impl& impl, const std::string& name)
      : node_(impl.outputs_node.CreateChild(name)),
        driver_name_(node_.CreateString("driver name", "unknown")),
        gain_info_(node_),
        device_underflows_(
            std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
                .event_name = "device underflows",
                .parent_node = node_,
                .impl = impl,
                .is_underflow = true,
                .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::OutputDevice,
            })),
        pipeline_underflows_(
            std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
                .event_name = "pipeline underflows",
                .parent_node = node_,
                .impl = impl,
                .is_underflow = true,
                .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::OutputPipeline,
            })) {}

  void StartSession(zx::time start_time) override {
    device_underflows_->StartSession(start_time);
    pipeline_underflows_->StartSession(start_time);
  }

  void StopSession(zx::time stop_time) override {
    device_underflows_->StopSession(stop_time);
    pipeline_underflows_->StopSession(stop_time);
  }

  void SetDriverName(const std::string& driver_name) override { driver_name_.Set(driver_name); }

  void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                   fuchsia::media::AudioGainValidFlags set_flags) override {
    gain_info_.Set(gain_info, set_flags);
  }

  void DeviceUnderflow(zx::time start_time, zx::time end_time) override {
    device_underflows_->Report(start_time, end_time);
  }

  void PipelineUnderflow(zx::time start_time, zx::time end_time) override {
    pipeline_underflows_->Report(start_time, end_time);
  }

 private:
  inspect::Node node_;
  inspect::StringProperty driver_name_;
  DeviceGainInfo gain_info_;
  std::unique_ptr<OverflowUnderflowTracker> device_underflows_;
  std::unique_ptr<OverflowUnderflowTracker> pipeline_underflows_;
};

class Reporter::InputDeviceImpl : public Reporter::InputDevice {
 public:
  InputDeviceImpl(Reporter::Impl& impl, const std::string& name)
      : node_(impl.inputs_node.CreateChild(name)),
        driver_name_(node_.CreateString("driver name", "unknown")),
        gain_info_(node_) {}

  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetDriverName(const std::string& driver_name) override { driver_name_.Set(driver_name); }

  void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                   fuchsia::media::AudioGainValidFlags set_flags) override {
    gain_info_.Set(gain_info, set_flags);
  }

 private:
  inspect::Node node_;
  inspect::StringProperty driver_name_;
  DeviceGainInfo gain_info_;
};

class ClientPort {
 public:
  ClientPort(inspect::Node& node)
      : sample_format_(node.CreateUint("sample format", 0)),
        channels_(node.CreateUint("channels", 0)),
        frames_per_second_(node.CreateUint("frames per second", 0)),
        payload_buffers_node_(node.CreateChild("payload buffers")),
        gain_db_(node.CreateDouble("gain db", 0.0)),
        muted_(node.CreateBool("muted", false)),
        set_gain_with_ramp_calls_(node.CreateUint("calls to SetGainWithRamp", 0)) {}

  void SetStreamType(const fuchsia::media::AudioStreamType& stream_type) {
    sample_format_.Set(static_cast<uint64_t>(stream_type.sample_format));
    channels_.Set(stream_type.channels);
    frames_per_second_.Set(stream_type.frames_per_second);
  }

  void SetGain(float gain_db) { gain_db_.Set(gain_db); }
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type) {
    set_gain_with_ramp_calls_.Add(1);
  }
  void SetMute(bool muted) { muted_.Set(muted); }

  void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    payload_buffers_.emplace(
        buffer_id,
        PayloadBuffer(payload_buffers_node_.CreateChild(std::to_string(buffer_id)), size));
  }

  void RemovePayloadBuffer(uint32_t buffer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    payload_buffers_.erase(buffer_id);
  }

  void SendPacket(const fuchsia::media::StreamPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto b = payload_buffers_.find(packet.payload_buffer_id);
    if (b == payload_buffers_.end()) {
      FX_LOGS(ERROR) << "Specified payload buffer not found";
      return;
    }
    b->second.packets_.Add(1);
  }

 private:
  inspect::UintProperty sample_format_;
  inspect::UintProperty channels_;
  inspect::UintProperty frames_per_second_;

  inspect::Node payload_buffers_node_;

  struct PayloadBuffer {
    PayloadBuffer(inspect::Node node, uint64_t size)
        : node_(std::move(node)),
          size_(node_.CreateUint("size", size)),
          packets_(node_.CreateUint("packets", 0)) {}

    inspect::Node node_;
    inspect::UintProperty size_;
    inspect::UintProperty packets_;
  };
  std::mutex mutex_;
  std::unordered_map<uint32_t, PayloadBuffer> payload_buffers_ FXL_GUARDED_BY(mutex_);

  inspect::DoubleProperty gain_db_;
  inspect::BoolProperty muted_;
  // Just counting these for now.
  inspect::UintProperty set_gain_with_ramp_calls_;
};

class Reporter::RendererImpl : public Reporter::Renderer {
 public:
  RendererImpl(Reporter::Impl& impl)
      : node_(impl.renderers_node.CreateChild(impl.NextRendererName())),
        client_port_(node_),
        min_lead_time_ns_(node_.CreateUint("min lead time (ns)", 0)),
        pts_continuity_threshold_seconds_(node_.CreateDouble("pts continuity threshold (s)", 0.0)),
        final_stream_gain_(node_.CreateDouble("final stream gain (post-volume) dbfs", 0.0)),
        usage_(node_.CreateString("usage", "default")),
        underflows_(std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
            .event_name = "underflows",
            .parent_node = node_,
            .impl = impl,
            .is_underflow = true,
            .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::Renderer,
        })) {}

  void StartSession(zx::time start_time) override { underflows_->StartSession(start_time); }
  void StopSession(zx::time stop_time) override { underflows_->StopSession(stop_time); }

  void SetUsage(RenderUsage usage) override { usage_.Set(RenderUsageToString(usage)); }
  void SetStreamType(const fuchsia::media::AudioStreamType& stream_type) override {
    client_port_.SetStreamType(stream_type);
  }
  void SetGain(float gain_db) override { client_port_.SetGain(gain_db); }
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type) override {
    client_port_.SetGainWithRamp(gain_db, duration, ramp_type);
  }
  void SetFinalGain(float gain_db) override { final_stream_gain_.Set(gain_db); }
  void SetMute(bool muted) override { client_port_.SetMute(muted); }

  void SetMinLeadTime(zx::duration min_lead_time) override {
    min_lead_time_ns_.Set(min_lead_time.to_nsecs());
  }
  void SetPtsContinuityThreshold(float threshold_seconds) override {
    pts_continuity_threshold_seconds_.Set(threshold_seconds);
  }

  void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) override {
    client_port_.AddPayloadBuffer(buffer_id, size);
  }
  void RemovePayloadBuffer(uint32_t buffer_id) override {
    client_port_.RemovePayloadBuffer(buffer_id);
  }
  void SendPacket(const fuchsia::media::StreamPacket& packet) override {
    client_port_.SendPacket(packet);
  }
  void Underflow(zx::time start_time, zx::time end_time) override {
    underflows_->Report(start_time, end_time);
  }

 private:
  inspect::Node node_;
  ClientPort client_port_;
  inspect::UintProperty min_lead_time_ns_;
  inspect::DoubleProperty pts_continuity_threshold_seconds_;
  inspect::DoubleProperty final_stream_gain_;
  inspect::StringProperty usage_;
  std::unique_ptr<OverflowUnderflowTracker> underflows_;
};

class Reporter::CapturerImpl : public Reporter::Capturer {
 public:
  CapturerImpl(Reporter::Impl& impl)
      : node_(impl.capturers_node.CreateChild(impl.NextCapturerName())),
        client_port_(node_),
        min_fence_time_ns_(node_.CreateUint("min fence time (ns)", 0)),
        usage_(node_.CreateString("usage", "default")),
        overflows_(std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
            .event_name = "overflows",
            .parent_node = node_,
            .impl = impl,
            .is_underflow = false,
            .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::Capturer,
        })) {}

  void StartSession(zx::time start_time) override { overflows_->StartSession(start_time); }
  void StopSession(zx::time stop_time) override { overflows_->StopSession(stop_time); }

  void SetUsage(CaptureUsage usage) override { usage_.Set(CaptureUsageToString(usage)); }

  void SetStreamType(const fuchsia::media::AudioStreamType& stream_type) override {
    client_port_.SetStreamType(stream_type);
  }
  void SetGain(float gain_db) override { client_port_.SetGain(gain_db); }
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type) override {
    client_port_.SetGainWithRamp(gain_db, duration, ramp_type);
  }
  void SetMute(bool muted) override { client_port_.SetMute(muted); }

  void SetMinFenceTime(zx::duration min_fence_time) override {
    min_fence_time_ns_.Set(min_fence_time.to_nsecs());
  }

  void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) override {
    client_port_.AddPayloadBuffer(buffer_id, size);
  }
  void SendPacket(const fuchsia::media::StreamPacket& packet) override {
    client_port_.SendPacket(packet);
  }
  void Overflow(zx::time start_time, zx::time end_time) override {
    overflows_->Report(start_time, end_time);
  }

 private:
  inspect::Node node_;
  ClientPort client_port_;
  inspect::UintProperty min_fence_time_ns_;
  inspect::StringProperty usage_;
  std::unique_ptr<OverflowUnderflowTracker> overflows_;
};

////////////////////////////////////////////////////////////////////////////////
// Reporter implementation

// static
Reporter& Reporter::Singleton() {
  std::lock_guard<std::mutex> lock(singleton_mutex);
  if (singleton_real) {
    return *singleton_real;
  }
  FX_LOGS_FIRST_N(INFO, 1)
      << "Creating reporting objects before the Reporter singleton has been initialized";
  return singleton_nop;
}

// static
void Reporter::InitializeSingleton(sys::ComponentContext& component_context,
                                   ThreadingModel& threading_model) {
  std::lock_guard<std::mutex> lock(singleton_mutex);
  if (singleton_real) {
    FX_LOGS(ERROR) << "Reporter::Singleton double initialized";
    return;
  }
  singleton_real = std::make_unique<Reporter>(component_context, threading_model);
}

Reporter::Reporter(sys::ComponentContext& component_context, ThreadingModel& threading_model)
    : impl_(std::make_unique<Impl>(component_context, threading_model)) {
  // This lock isn't necessary, but the lock analysis can't tell that.
  std::lock_guard<std::mutex> lock(mutex_);
  InitInspect();
  InitCobalt();
}

void Reporter::InitInspect() {
  impl_->inspector = std::make_unique<sys::ComponentInspector>(&impl_->component_context);
  inspect::Node& root_node = impl_->inspector->root();

  impl_->failed_to_open_device_count = root_node.CreateUint("count of failures to open device", 0);
  impl_->failed_to_obtain_fdio_service_channel_count =
      root_node.CreateUint("count of failures to obtain device fdio service channel", 0);
  impl_->failed_to_obtain_stream_channel_count =
      root_node.CreateUint("count of failures to obtain device stream channel", 0);
  impl_->failed_to_start_device_count =
      root_node.CreateUint("count of failures to start a device", 0);

  impl_->outputs_node = root_node.CreateChild("output devices");
  impl_->inputs_node = root_node.CreateChild("input devices");
  impl_->renderers_node = root_node.CreateChild("renderers");
  impl_->capturers_node = root_node.CreateChild("capturers");
}

void Reporter::InitCobalt() {
  impl_->component_context.svc()->Connect(impl_->cobalt_factory.NewRequest());
  if (!impl_->cobalt_factory) {
    FX_LOGS(ERROR) << "audio_core could not connect to cobalt. No metrics will be captured.";
    return;
  }

  impl_->cobalt_factory->CreateLoggerFromProjectId(
      kProjectId, impl_->cobalt_logger.NewRequest(), [this](fuchsia::cobalt::Status status) {
        if (status != fuchsia::cobalt::Status::OK) {
          FX_LOGS(ERROR) << "audio_core could not create Cobalt logger, status = "
                         << fidl::ToUnderlying(status);
          std::lock_guard<std::mutex> lock(mutex_);
          impl_->cobalt_logger = nullptr;
        }
      });
}

std::unique_ptr<Reporter::OutputDevice> Reporter::CreateOutputDevice(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return std::make_unique<OutputDeviceNop>();
  }
  return std::make_unique<Reporter::OutputDeviceImpl>(*impl_, name);
}

std::unique_ptr<Reporter::InputDevice> Reporter::CreateInputDevice(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return std::make_unique<InputDeviceNop>();
  }
  return std::make_unique<Reporter::InputDeviceImpl>(*impl_, name);
}

std::unique_ptr<Reporter::Renderer> Reporter::CreateRenderer() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return std::make_unique<RendererNop>();
  }
  return std::make_unique<Reporter::RendererImpl>(*impl_);
}

std::unique_ptr<Reporter::Capturer> Reporter::CreateCapturer() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return std::make_unique<CapturerNop>();
  }
  return std::make_unique<Reporter::CapturerImpl>(*impl_);
}

void Reporter::FailedToOpenDevice(const std::string& name, bool is_input, int err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return;
  }
  impl_->failed_to_open_device_count.Add(1);
}

void Reporter::FailedToObtainFdioServiceChannel(const std::string& name, bool is_input,
                                                zx_status_t status) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return;
  }
  impl_->failed_to_obtain_fdio_service_channel_count.Add(1);
}

void Reporter::FailedToObtainStreamChannel(const std::string& name, bool is_input,
                                           zx_status_t status) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return;
  }
  impl_->failed_to_obtain_stream_channel_count.Add(1);
}

void Reporter::FailedToStartDevice(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return;
  }
  impl_->failed_to_start_device_count.Add(1);
}

//////////////////////////////////////////////////////////////////////////////////
// OverflowUnderflowTracker implementation

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
      impl_(args.impl),
      cobalt_component_id_(args.cobalt_component_id),
      cobalt_event_duration_metric_id_(args.is_underflow ? kAudioUnderflowDurationMetricId
                                                         : kAudioOverflowDurationMetricId),
      cobalt_time_since_last_event_or_session_start_metric_id_(
          args.is_underflow ? kAudioTimeSinceLastUnderflowOrSessionStartMetricId
                            : kAudioTimeSinceLastOverflowOrSessionStartMetricId) {
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

Reporter::OverflowUnderflowTracker::~OverflowUnderflowTracker() {
  bool started;
  {
    // Technically a lock is not required here, but the lock analysis can't tell that.
    std::lock_guard<std::mutex> lock(mutex_);
    started = (state_ == State::Started);
  }
  if (started) {
    StopSession(zx::clock::get_monotonic());
  }
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
  restart_session_timer_.PostDelayed(impl_.threading_model.IoDomain().dispatcher(),
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
  restart_session_timer_.PostDelayed(impl_.threading_model.IoDomain().dispatcher(),
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
  auto& logger = impl_.cobalt_logger;
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

}  // namespace media::audio
