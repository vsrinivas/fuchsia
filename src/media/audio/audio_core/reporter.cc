// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/reporter.h"

#include <lib/syslog/cpp/macros.h>

#include <queue>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/media_metrics_registry.cb.h"

namespace media::audio {

////////////////////////////////////////////////////////////////////////////////
// Singletons

namespace {
static std::mutex singleton_mutex;
static Reporter* const singleton_nop = new Reporter();
static Reporter* singleton_real;
}  // namespace

////////////////////////////////////////////////////////////////////////////////
// No-op implementations, used before the Reporter has been initialized

namespace {
class OutputDeviceNop : public Reporter::OutputDevice {
 public:
  void Destroy() override {}

  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetDriverInfo(const AudioDriver& driver) override {}
  void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                   fuchsia::media::AudioGainValidFlags set_flags) override {}
  void DeviceUnderflow(zx::time start_time, zx::time end_time) override {}
  void PipelineUnderflow(zx::time start_time, zx::time end_time) override {}
};

class InputDeviceNop : public Reporter::InputDevice {
 public:
  void Destroy() override {}

  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetDriverInfo(const AudioDriver& driver) override {}
  void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                   fuchsia::media::AudioGainValidFlags set_flags) override {}
};

class RendererNop : public Reporter::Renderer {
 public:
  void Destroy() override {}

  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetUsage(RenderUsage usage) override {}
  void SetFormat(const Format& format) override {}
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
  void Destroy() override {}

  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetUsage(CaptureUsage usage) override {}
  void SetFormat(const Format& format) override {}
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

class Reporter::ObjectTracker {
 public:
  ObjectTracker(Reporter::Impl& impl, AudioObjectsCreatedMetricDimensionObjectType object_type)
      : impl_(impl), object_type_(object_type) {}

  void SetFormat(const Format& f) { format_ = f; }

  // Marks the object "enabled", which triggers a cobalt metric increment.
  void Enable() {
    // Ignore when cobalt is disabled.
    auto& logger = impl_.cobalt_logger;
    if (!logger) {
      return;
    }

    // Log exactly once.
    if (enabled_ || !format_) {
      return;
    }
    enabled_ = true;

    AudioObjectsCreatedMetricDimensionSampleFormat sample_format;
    switch (format_->sample_format()) {
      case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
        sample_format = AudioObjectsCreatedMetricDimensionSampleFormat::Uint8;
        break;
      case fuchsia::media::AudioSampleFormat::SIGNED_16:
        sample_format = AudioObjectsCreatedMetricDimensionSampleFormat::Int16;
        break;
      case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
        sample_format = AudioObjectsCreatedMetricDimensionSampleFormat::Int24;
        break;
      case fuchsia::media::AudioSampleFormat::FLOAT:
        sample_format = AudioObjectsCreatedMetricDimensionSampleFormat::Float32;
        break;
      default:
        sample_format = AudioObjectsCreatedMetricDimensionSampleFormat::Other;
        break;
    }
    AudioObjectsCreatedMetricDimensionChannels channels;
    switch (format_->channels()) {
      case 1:
        channels = AudioObjectsCreatedMetricDimensionChannels::Chan1;
        break;
      case 2:
        channels = AudioObjectsCreatedMetricDimensionChannels::Chan2;
        break;
      case 4:
        channels = AudioObjectsCreatedMetricDimensionChannels::Chan4;
        break;
      default:
        channels = AudioObjectsCreatedMetricDimensionChannels::Other;
        break;
    }
    AudioObjectsCreatedMetricDimensionFrameRate frame_rate;
    switch (format_->frames_per_second()) {
      case 16000:
        frame_rate = AudioObjectsCreatedMetricDimensionFrameRate::Rate16000;
        break;
      case 44100:
        frame_rate = AudioObjectsCreatedMetricDimensionFrameRate::Rate44100;
        break;
      case 48000:
        frame_rate = AudioObjectsCreatedMetricDimensionFrameRate::Rate48000;
        break;
      case 96000:
        frame_rate = AudioObjectsCreatedMetricDimensionFrameRate::Rate96000;
        break;
      default:
        frame_rate = AudioObjectsCreatedMetricDimensionFrameRate::Other;
        break;
    }
    auto e = fuchsia::cobalt::CobaltEvent{
        .metric_id = kAudioObjectsCreatedMetricId,
        .event_codes = {object_type_, sample_format, channels, frame_rate},
        .payload =
            fuchsia::cobalt::EventPayload::WithEventCount(fuchsia::cobalt::CountEvent{.count = 1}),
    };
    impl_.threading_model.FidlDomain().PostTask([&logger, e = std::move(e)]() mutable {
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
    });
  }

 private:
  Reporter::Impl& impl_;
  AudioObjectsCreatedMetricDimensionObjectType object_type_;
  std::optional<Format> format_;
  bool enabled_ = false;
};

class FormatInfo {
 public:
  FormatInfo(inspect::Node& parent_node, const std::string& name)
      : node_(parent_node.CreateChild(name)),
        sample_format_(node_.CreateString("sample format", "unknown")),
        channels_(node_.CreateUint("channels", 0)),
        frames_per_second_(node_.CreateUint("frames per second", 0)) {}

  void Set(const Format& f) {
    switch (f.sample_format()) {
      case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
        sample_format_.Set("UNSIGNED_8");
        break;
      case fuchsia::media::AudioSampleFormat::SIGNED_16:
        sample_format_.Set("SIGNED_16");
        break;
      case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
        sample_format_.Set("SIGNED_24_IN_32");
        break;
      case fuchsia::media::AudioSampleFormat::FLOAT:
        sample_format_.Set("FLOAT");
        break;
      default:
        sample_format_.Set("unknown");
        FX_LOGS(ERROR) << "Unhandled sample stream_type type "
                       << fidl::ToUnderlying(f.sample_format());
        break;
    }
    channels_.Set(f.channels());
    frames_per_second_.Set(f.frames_per_second());
  }

 private:
  inspect::Node node_;
  inspect::StringProperty sample_format_;
  inspect::UintProperty channels_;
  inspect::UintProperty frames_per_second_;
};

class Reporter::DeviceDriverInfo {
 public:
  DeviceDriverInfo(inspect::Node& parent_node, ObjectTracker&& object_tracker)
      : node_(parent_node.CreateChild("driver")),
        name_(node_.CreateString("name", "unknown")),
        total_delay_(node_.CreateUint("external delay + fifo delay (ns)", 0)),
        external_delay_(node_.CreateUint("external delay (ns)", 0)),
        fifo_delay_(node_.CreateUint("fifo delay (ns)", 0)),
        fifo_depth_(node_.CreateUint("fifo depth in frames", 0)),
        format_(parent_node, "format"),
        object_tracker_(std::move(object_tracker)) {}

  void Set(const AudioDriver& d) {
    name_.Set(d.manufacturer_name() + ' ' + d.product_name());
    total_delay_.Set((d.external_delay() + d.fifo_depth_duration()).get());
    external_delay_.Set(d.external_delay().get());
    fifo_delay_.Set(d.fifo_depth_duration().get());
    fifo_depth_.Set(d.fifo_depth_frames());
    if (auto f = d.GetFormat(); f.has_value()) {
      format_.Set(*f);
      object_tracker_.SetFormat(*f);
      object_tracker_.Enable();
    }
  }

 private:
  inspect::Node node_;
  inspect::StringProperty name_;
  inspect::UintProperty total_delay_;
  inspect::UintProperty external_delay_;
  inspect::UintProperty fifo_delay_;
  inspect::UintProperty fifo_depth_;
  FormatInfo format_;
  ObjectTracker object_tracker_;
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

class Reporter::ThermalStateTransition {
 public:
  explicit ThermalStateTransition(Reporter::Impl& impl, uint32_t state)
      : node_(impl.thermal_state_transitions_node.CreateChild(impl.NextThermalTransitionName())),
        state_(node_.CreateString("state", state ? std::to_string(state) : "normal")),
        active_(node_.CreateBool("active", true)),
        duration_(node_.CreateLazyValues(
            "ThermalStateTransitionDuration",
            [this] {
              std::lock_guard<std::mutex> lock(mutex_);
              inspect::Inspector i;
              i.GetRoot().CreateUint(
                  "duration (ns)",
                  (alive_ ? zx::clock::get_monotonic() - start_time_ : past_duration_).get(), &i);
              return fit::make_ok_promise(std::move(i));
            })),
        start_time_(zx::clock::get_monotonic()) {}

  void Destroy() {
    std::lock_guard<std::mutex> lock(mutex_);
    past_duration_ = zx::clock::get_monotonic() - start_time_;
    alive_ = false;
    active_.Set(false);
  }

 private:
  inspect::Node node_;
  inspect::StringProperty state_;
  inspect::BoolProperty active_;
  inspect::LazyNode duration_;

  const zx::time start_time_;

  std::mutex mutex_;
  bool alive_ FXL_GUARDED_BY(mutex_) = true;
  zx::duration past_duration_ FXL_GUARDED_BY(mutex_);
};

class Reporter::ThermalStateTracker {
 public:
  ThermalStateTracker(
      Reporter::Impl& impl,
      Container<ThermalStateTransition, kThermalStatesToCache>& thermal_state_transitions);

  void SetNumThermalStates(size_t num);
  void SetThermalState(uint32_t state);

  void DropLastTransition() { last_transition_.Drop(); }

 private:
  using CobaltStateTransition = AudioThermalStateTransitionsMetricDimensionStateTransition;
  static constexpr std::array kCobaltStateTransitions = {
      CobaltStateTransition::Normal, CobaltStateTransition::State1, CobaltStateTransition::State2};

  struct State {
    // Total duration this state has been active, not counting
    // the current activation if this is the current state.
    zx::duration past_duration;
    // Set if this is the current thermal state.
    std::optional<zx::time> current_activation_time;
    // Running total of transitions to State.
    uint32_t total_transitions;

    inspect::Node node;
    inspect::LazyNode duration;

    void Activate();
    void Deactivate();
  };

  void LogCobaltStateTransition(State& state, CobaltStateTransition event);

  Reporter::Impl& impl_;
  Container<ThermalStateTransition, kThermalStatesToCache>& thermal_state_transitions_;
  inspect::Node root_;
  inspect::UintProperty num_thermal_states_;

  uint32_t active_state_;
  std::unordered_map<uint32_t, State> states_;
  Container<ThermalStateTransition, kThermalStatesToCache>::Ptr last_transition_;
};

class Reporter::OutputDeviceImpl : public Reporter::OutputDevice {
 public:
  OutputDeviceImpl(Reporter::Impl& impl, const std::string& name, const std::string& thread_name)
      : node_(impl.outputs_node.CreateChild(name)),
        thread_name_(node_.CreateString("mixer thread name", thread_name)),
        driver_info_(
            node_, ObjectTracker(impl, AudioObjectsCreatedMetricDimensionObjectType::OutputDevice)),
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
            })) {
    time_since_death_ = node_.CreateLazyValues("OutputDeviceTimeSinceDeath", [this] {
      inspect::Inspector i;
      i.GetRoot().CreateUint(
          "time since death (ns)",
          time_of_death_ ? (zx::clock::get_monotonic() - time_of_death_.value()).get() : 0, &i);
      return fit::make_ok_promise(std::move(i));
    });
  }

  void Destroy() override { time_of_death_ = zx::clock::get_monotonic(); }

  void StartSession(zx::time start_time) override {
    device_underflows_->StartSession(start_time);
    pipeline_underflows_->StartSession(start_time);
  }

  void StopSession(zx::time stop_time) override {
    device_underflows_->StopSession(stop_time);
    pipeline_underflows_->StopSession(stop_time);
  }

  void SetDriverInfo(const AudioDriver& driver) override { driver_info_.Set(driver); }

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
  inspect::LazyNode time_since_death_;
  inspect::StringProperty thread_name_;
  DeviceDriverInfo driver_info_;
  DeviceGainInfo gain_info_;
  std::unique_ptr<OverflowUnderflowTracker> device_underflows_;
  std::unique_ptr<OverflowUnderflowTracker> pipeline_underflows_;
  std::optional<zx::time> time_of_death_;
};

class Reporter::InputDeviceImpl : public Reporter::InputDevice {
 public:
  InputDeviceImpl(Reporter::Impl& impl, const std::string& name, const std::string& thread_name)
      : node_(impl.inputs_node.CreateChild(name)),
        thread_name_(node_.CreateString("mixer thread name", thread_name)),
        driver_info_(
            node_, ObjectTracker(impl, AudioObjectsCreatedMetricDimensionObjectType::InputDevice)),
        gain_info_(node_) {
    time_since_death_ = node_.CreateLazyValues("InputDeviceTimeSinceDeath", [this] {
      inspect::Inspector i;
      i.GetRoot().CreateUint(
          "time since death (ns)",
          time_of_death_ ? (zx::clock::get_monotonic() - time_of_death_.value()).get() : 0, &i);
      return fit::make_ok_promise(std::move(i));
    });
  }

  void Destroy() override { time_of_death_ = zx::clock::get_monotonic(); }

  void StartSession(zx::time start_time) override {}
  void StopSession(zx::time stop_time) override {}

  void SetDriverInfo(const AudioDriver& driver) override { driver_info_.Set(driver); }

  void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                   fuchsia::media::AudioGainValidFlags set_flags) override {
    gain_info_.Set(gain_info, set_flags);
  }

 private:
  inspect::Node node_;
  inspect::LazyNode time_since_death_;
  inspect::StringProperty thread_name_;
  DeviceDriverInfo driver_info_;
  DeviceGainInfo gain_info_;
  std::optional<zx::time> time_of_death_;
};

class Reporter::ClientPort {
 public:
  ClientPort(inspect::Node& node, ObjectTracker&& object_tracker)
      : object_tracker_(std::move(object_tracker)),
        format_(node, "format"),
        payload_buffers_node_(node.CreateChild("payload buffers")),
        gain_db_(node.CreateDouble("gain db", 0.0)),
        muted_(node.CreateBool("muted", false)),
        set_gain_with_ramp_calls_(node.CreateUint("calls to SetGainWithRamp", 0)) {}

  void SetFormat(const Format& format) {
    object_tracker_.SetFormat(format);
    format_.Set(format);
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
    object_tracker_.Enable();
    std::lock_guard<std::mutex> lock(mutex_);
    auto b = payload_buffers_.find(packet.payload_buffer_id);
    if (b == payload_buffers_.end()) {
      FX_LOGS(ERROR) << "Specified payload buffer not found";
      return;
    }
    b->second.packets_.Add(1);
  }

 private:
  ObjectTracker object_tracker_;
  FormatInfo format_;
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
        client_port_(node_,
                     ObjectTracker(impl, AudioObjectsCreatedMetricDimensionObjectType::Renderer)),
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
        })) {
    time_since_death_ = node_.CreateLazyValues("RendererTimeSinceDeath", [this] {
      inspect::Inspector i;
      i.GetRoot().CreateUint(
          "time since death (ns)",
          time_of_death_ ? (zx::clock::get_monotonic() - time_of_death_.value()).get() : 0, &i);
      return fit::make_ok_promise(std::move(i));
    });
  }

  void Destroy() override { time_of_death_ = zx::clock::get_monotonic(); }

  void StartSession(zx::time start_time) override { underflows_->StartSession(start_time); }
  void StopSession(zx::time stop_time) override { underflows_->StopSession(stop_time); }

  void SetUsage(RenderUsage usage) override { usage_.Set(RenderUsageToString(usage)); }
  void SetFormat(const Format& format) override { client_port_.SetFormat(format); }
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
  inspect::LazyNode time_since_death_;
  inspect::UintProperty min_lead_time_ns_;
  inspect::DoubleProperty pts_continuity_threshold_seconds_;
  inspect::DoubleProperty final_stream_gain_;
  inspect::StringProperty usage_;
  std::unique_ptr<OverflowUnderflowTracker> underflows_;
  std::optional<zx::time> time_of_death_;
};

class Reporter::CapturerImpl : public Reporter::Capturer {
 public:
  CapturerImpl(Reporter::Impl& impl, const std::string& thread_name)
      : node_(impl.capturers_node.CreateChild(impl.NextCapturerName())),
        client_port_(node_,
                     ObjectTracker(impl, AudioObjectsCreatedMetricDimensionObjectType::Capturer)),
        min_fence_time_ns_(node_.CreateUint("min fence time (ns)", 0)),
        usage_(node_.CreateString("usage", "default")),
        thread_name_(node_.CreateString("mixer thread name", thread_name)),
        overflows_(std::make_unique<OverflowUnderflowTracker>(OverflowUnderflowTracker::Args{
            .event_name = "overflows",
            .parent_node = node_,
            .impl = impl,
            .is_underflow = false,
            .cobalt_component_id = AudioSessionDurationMetricDimensionComponent::Capturer,
        })) {
    time_since_death_ = node_.CreateLazyValues("CapturerTimeSinceDeath", [this] {
      inspect::Inspector i;
      i.GetRoot().CreateUint(
          "time since death (ns)",
          time_of_death_ ? (zx::clock::get_monotonic() - time_of_death_.value()).get() : 0, &i);
      return fit::make_ok_promise(std::move(i));
    });
  }

  void Destroy() override { time_of_death_ = zx::clock::get_monotonic(); }

  void StartSession(zx::time start_time) override { overflows_->StartSession(start_time); }
  void StopSession(zx::time stop_time) override { overflows_->StopSession(stop_time); }

  void SetUsage(CaptureUsage usage) override { usage_.Set(CaptureUsageToString(usage)); }
  void SetFormat(const Format& format) override { client_port_.SetFormat(format); }
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
  inspect::LazyNode time_since_death_;
  inspect::UintProperty min_fence_time_ns_;
  inspect::StringProperty usage_;
  inspect::StringProperty thread_name_;
  std::unique_ptr<OverflowUnderflowTracker> overflows_;
  std::optional<zx::time> time_of_death_;
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
  return *singleton_nop;
}

// static
void Reporter::InitializeSingleton(sys::ComponentContext& component_context,
                                   ThreadingModel& threading_model) {
  std::lock_guard<std::mutex> lock(singleton_mutex);
  if (singleton_real) {
    FX_LOGS(ERROR) << "Reporter::Singleton double initialized";
    return;
  }
  singleton_real = new Reporter(component_context, threading_model);
}

Reporter::Reporter(sys::ComponentContext& component_context, ThreadingModel& threading_model)
    : impl_(std::make_unique<Impl>(component_context, threading_model)) {
  // This lock isn't necessary, but the lock analysis can't tell that.
  std::lock_guard<std::mutex> lock(mutex_);
  InitInspect();
  InitCobalt();
}

Reporter::~Reporter() { impl_->thermal_state_tracker->DropLastTransition(); }

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
  impl_->mixer_clock_skew_discontinuities =
      root_node.CreateLinearIntHistogram("mixer clock skew discontinuities (error in ns)",
                                         ZX_MSEC(-100),  // floor
                                         ZX_MSEC(2),     // step size
                                         100);           // buckets range from -100ms to +100ms

  impl_->outputs_node = root_node.CreateChild("output devices");
  impl_->inputs_node = root_node.CreateChild("input devices");
  impl_->renderers_node = root_node.CreateChild("renderers");
  impl_->capturers_node = root_node.CreateChild("capturers");
  impl_->thermal_state_transitions_node = root_node.CreateChild("thermal state transitions");

  impl_->thermal_state_tracker =
      std::make_unique<ThermalStateTracker>(*impl_, thermal_state_transitions_);
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

Reporter::Container<Reporter::OutputDevice, Reporter::kObjectsToCache>::Ptr
Reporter::CreateOutputDevice(const std::string& name, const std::string& thread_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return outputs_.New(
      impl_ ? static_cast<OutputDevice*>(new OutputDeviceImpl(*impl_, name, thread_name))
            : static_cast<OutputDevice*>(new OutputDeviceNop));
}

Reporter::Container<Reporter::InputDevice, Reporter::kObjectsToCache>::Ptr
Reporter::CreateInputDevice(const std::string& name, const std::string& thread_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return inputs_.New(impl_
                         ? static_cast<InputDevice*>(new InputDeviceImpl(*impl_, name, thread_name))
                         : static_cast<InputDevice*>(new InputDeviceNop));
}

Reporter::Container<Reporter::Renderer, Reporter::kObjectsToCache>::Ptr Reporter::CreateRenderer() {
  std::lock_guard<std::mutex> lock(mutex_);
  return renderers_.New(impl_ ? static_cast<Renderer*>(new RendererImpl(*impl_))
                              : static_cast<Renderer*>(new RendererNop));
}

Reporter::Container<Reporter::Capturer, Reporter::kObjectsToCache>::Ptr Reporter::CreateCapturer(
    const std::string& thread_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return capturers_.New(impl_ ? static_cast<Capturer*>(new CapturerImpl(*impl_, thread_name))
                              : static_cast<Capturer*>(new CapturerNop));
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

void Reporter::MixerClockSkewDiscontinuity(zx::duration clock_error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return;
  }
  impl_->mixer_clock_skew_discontinuities.Insert(clock_error.get());
}

void Reporter::SetNumThermalStates(size_t num) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return;
  }
  impl_->thermal_state_tracker->SetNumThermalStates(num);
}

void Reporter::SetThermalState(uint32_t state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!impl_) {
    return;
  }
  impl_->thermal_state_tracker->SetThermalState(state);
}

Reporter::Impl::Impl(sys::ComponentContext& cc, ThreadingModel& tm)
    : component_context(cc), threading_model(tm) {}

Reporter::Impl::~Impl() {}

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

  impl_.threading_model.FidlDomain().PostTask([&logger, e = std::move(e)]() mutable {
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
  });
}

//////////////////////////////////////////////////////////////////////////////////
// ThermalStateTracker implementation

Reporter::ThermalStateTracker::ThermalStateTracker(
    Reporter::Impl& impl,
    Container<ThermalStateTransition, kThermalStatesToCache>& thermal_state_transitions)
    : impl_(impl),
      thermal_state_transitions_(thermal_state_transitions),
      root_(impl.inspector->root().CreateChild("thermal state")),
      num_thermal_states_(root_.CreateUint("num thermal states", 1)),
      active_state_(0),
      last_transition_(
          thermal_state_transitions_.New(new ThermalStateTransition(impl_, active_state_))) {
  states_[active_state_] = State({.node = root_.CreateChild("normal")});
  states_[active_state_].Activate();
  LogCobaltStateTransition(states_[active_state_], kCobaltStateTransitions[active_state_]);
}

void Reporter::ThermalStateTracker::SetNumThermalStates(size_t num) {
  num_thermal_states_.Set(num);
}

void Reporter::ThermalStateTracker::SetThermalState(uint32_t state) {
  if (active_state_ == state) {
    return;
  }
  states_[active_state_].Deactivate();

  if (states_.find(state) == states_.end()) {
    states_[state] = State({.node = root_.CreateChild(state ? std::to_string(state) : "normal")});
  }
  states_[state].Activate();
  last_transition_ = thermal_state_transitions_.New(new ThermalStateTransition(impl_, state));
  LogCobaltStateTransition(states_[state], kCobaltStateTransitions[state]);

  // Set |active_state_| after all activation steps are complete.
  active_state_ = state;
}

void Reporter::ThermalStateTracker::State::Activate() {
  current_activation_time = zx::clock::get_monotonic();

  // If state has never been activated, set duration LazyNode.
  if (!past_duration.get()) {
    duration = node.CreateLazyValues("TotalThermalStateDuration", [this] {
      inspect::Inspector i;
      i.GetRoot().CreateUint("total duration (ns)",
                             (current_activation_time ? past_duration + zx::clock::get_monotonic() -
                                                            current_activation_time.value()
                                                      : past_duration)
                                 .get(),
                             &i);
      return fit::make_ok_promise(std::move(i));
    });
  }
}

void Reporter::ThermalStateTracker::State::Deactivate() {
  past_duration += (zx::clock::get_monotonic() - current_activation_time.value());
  current_activation_time = std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Thermal State Cobalt Metrics
//
// A per-device histogram is generated for each thermal state. Transitions into the state are
// counted by marking "1" in each bucket up to N total transitions since boot.
//
// For example, if a device has N transitions to state 2 since boot, the state 2 histogram
// will be:
//
//     value: [1] ... [1]  [0]  [0] ...
//    bucket:  0  ...  N   N+1  N+2 ...
//
// For each thermal state, Cobalt will aggregate the per-device histograms into one for all
// devices, such that each bucket counts the number of devices that have reached that bucket's
// thermal state count. Post-processing in the custom dashboard will reduce these histograms to
// give an accurate representation of the total number of devices that have reached each thermal
// state transition count.
////////////////////////////////////////////////////////////////////////////////////////////////
void Reporter::ThermalStateTracker::LogCobaltStateTransition(State& state,
                                                             CobaltStateTransition event) {
  auto& logger = impl_.cobalt_logger;
  if (!logger) {
    return;
  }

  if (auto transition_count = ++state.total_transitions; transition_count > 50) {
    FX_LOGS_FIRST_N(INFO, 10)
        << "Cobalt logging of audio_thermal_state_transitions has exceeded maximum of 50: " << event
        << " = " << transition_count << "transitions";
  } else {
    std::vector<fuchsia::cobalt::HistogramBucket> histogram{
        {.index = transition_count, .count = 1}};
    logger->LogIntHistogram(kAudioThermalStateTransitionsMetricId, event, "" /*component*/,
                            histogram, [](auto) {});
  }
}

}  // namespace media::audio
