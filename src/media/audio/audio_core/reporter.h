// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/inspect/cpp/component.h>

#include <unordered_map>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/threading_model.h"

#ifndef ENABLE_REPORTER
#define ENABLE_REPORTER 1
#endif

namespace media::audio {

#if ENABLE_REPORTER

class AudioDevice;

// A singleton instance of |Reporter| handles instrumentation concerns (e.g.
// exposing information via inspect, cobalt, etc) for an audio_core instance.
// The idea is to make instrumentation as simple as possible for the code that
// does the real work. This class implements methods corresponding to the
// events that need to be reported in terms that make the most sense for the
// caller. Calls to those methods are made using the |REPORT| macro, which is
// notationally simple:
//
//     REPORT(ThatThingHappened(details, more_details));
//
// Use of the macro also allows instrumentation to be dropped from the build
// using a simple gn argument, if desired (by setting ENABLE_REPORTER to 0).
// The reporter is provisioned by calling a creation method. This is done in main.cc:
//
//    Reporter::CreateSingleton(...);
//
// Reporting through REPORT will have no effect if this is not called.
// Note that calls to methods such as |AddingDevice| allocate resources, which
// should be deallocated by e.g. |RemovingDevice|. For the time being, this
// approach seems preferable to making client code hold RAII objects.
//
// For the time being, this class is single-threaded. Some support for multi-
// thread access will be required when traffic metrics are added.
//
// TODO(dalesat): Add traffic metrics.
//
// TODO(35741): Allow Cobalt and Inspect to be independently disabled. We may limit Inspect to
// pre-release or developer builds, whereas Cobalt would be enabled even in production.
//
class Reporter {
 public:
  static void CreateSingleton(sys::ComponentContext& component_context,
                              ThreadingModel& threading_model);
  static Reporter* Singleton();

  Reporter(sys::ComponentContext& component_context, ThreadingModel& threading_model);
  const inspect::Inspector& inspector() { return *inspector_->inspector(); }

  ////////////////////////////////////////////////////////////////////////////
  // The following methods are intended to be called using REPORT.

  // Devices.
  void FailedToOpenDevice(const std::string& name, bool is_input, int err);
  void FailedToObtainFdioServiceChannel(const std::string& name, bool is_input, zx_status_t status);
  void FailedToObtainStreamChannel(const std::string& name, bool is_input, zx_status_t status);
  void AddingDevice(const std::string& name, const AudioDevice& device);
  void RemovingDevice(const AudioDevice& device);
  void DeviceStartupFailed(const AudioDevice& device);
  void IgnoringDevice(const AudioDevice& device);
  void ActivatingDevice(const AudioDevice& device);
  void SettingDeviceGainInfo(const AudioDevice& device,
                             const fuchsia::media::AudioGainInfo& gain_info,
                             fuchsia::media::AudioGainValidFlags set_flags);

  // Renderers.
  void AddingRenderer(const fuchsia::media::AudioRenderer& renderer);
  void RemovingRenderer(const fuchsia::media::AudioRenderer& renderer);
  void SettingRendererStreamType(const fuchsia::media::AudioRenderer& renderer,
                                 const fuchsia::media::AudioStreamType& stream_type);
  void AddingRendererPayloadBuffer(const fuchsia::media::AudioRenderer& renderer,
                                   uint32_t buffer_id, uint64_t size);
  void RemovingRendererPayloadBuffer(const fuchsia::media::AudioRenderer& renderer,
                                     uint32_t buffer_id);
  void SendingRendererPacket(const fuchsia::media::AudioRenderer& renderer,
                             const fuchsia::media::StreamPacket& packet);
  void SettingRendererGain(const fuchsia::media::AudioRenderer& renderer, float gain_db);
  void SettingRendererGainWithRamp(const fuchsia::media::AudioRenderer& renderer, float gain_db,
                                   zx::duration duration,
                                   fuchsia::media::audio::RampType ramp_type);
  void SettingRendererFinalGain(const fuchsia::media::AudioRenderer& renderer, float gain_db);
  void SettingRendererMute(const fuchsia::media::AudioRenderer& renderer, bool muted);
  void SettingRendererMinLeadTime(const fuchsia::media::AudioRenderer& renderer,
                                  zx::duration min_lead_time);
  void SettingRendererPtsContinuityThreshold(const fuchsia::media::AudioRenderer& renderer,
                                             float threshold_seconds);

  // Capturers.
  void AddingCapturer(const fuchsia::media::AudioCapturer& capturer);
  void RemovingCapturer(const fuchsia::media::AudioCapturer& capturer);
  void SettingCapturerStreamType(const fuchsia::media::AudioCapturer& capturer,
                                 const fuchsia::media::AudioStreamType& stream_type);
  void AddingCapturerPayloadBuffer(const fuchsia::media::AudioCapturer& capturer,
                                   uint32_t buffer_id, uint64_t size);
  void SendingCapturerPacket(const fuchsia::media::AudioCapturer& capturer,
                             const fuchsia::media::StreamPacket& packet);
  void SettingCapturerGain(const fuchsia::media::AudioCapturer& capturer, float gain_db);
  void SettingCapturerGainWithRamp(const fuchsia::media::AudioCapturer& capturer, float gain_db,
                                   zx::duration duration,
                                   fuchsia::media::audio::RampType ramp_type);
  void SettingCapturerMute(const fuchsia::media::AudioCapturer& capturer, bool muted);
  void SettingCapturerMinFenceTime(const fuchsia::media::AudioCapturer& capturer,
                                   zx::duration min_fence_time);

  // Access to overflow/underflow trackers.
  void OutputDeviceStartSession(const AudioDevice& device, zx::time start_time);
  void OutputDeviceStopSession(const AudioDevice& device, zx::time stop_time);
  void OutputDeviceUnderflow(const AudioDevice& device, zx::time start_time, zx::time stop_time);

  void OutputPipelineStartSession(const AudioDevice& device, zx::time start_time);
  void OutputPipelineStopSession(const AudioDevice& device, zx::time stop_time);
  void OutputPipelineUnderflow(const AudioDevice& device, zx::time start_time, zx::time stop_time);

  void RendererStartSession(const fuchsia::media::AudioRenderer& renderer, zx::time start_time);
  void RendererStopSession(const fuchsia::media::AudioRenderer& renderer, zx::time stop_time);
  void RendererUnderflow(const fuchsia::media::AudioRenderer& renderer, zx::time start_time,
                         zx::time stop_time);

  void CapturerStartSession(const fuchsia::media::AudioCapturer& capturer, zx::time start_time);
  void CapturerStopSession(const fuchsia::media::AudioCapturer& capturer, zx::time stop_time);
  void CapturerOverflow(const fuchsia::media::AudioCapturer& capturer, zx::time start_time,
                        zx::time stop_time);

 private:
  friend class OverflowUnderflowTracker;
  void InitInspect();
  void InitCobalt();

  // This class tracks metrics for a single kind of overflow or underflow event.
  // All times use the system monotonic clock. Thread safe.
  class OverflowUnderflowTracker {
   public:
    // Trackers begin in a "stopped" state and must move to a "started" state before metrics
    // can be reported. The Start/Stop events are intended to mirror higher-level Play/Pause
    // or Record/Stop events.
    void StartSession(zx::time start_time);
    void StopSession(zx::time stop_time);

    // Returns true iff a session has been started.
    bool Started();

    // Report an event with the given start and end times.
    void Report(zx::time start_time, zx::time end_time);

    struct Args {
      uint32_t component;
      std::string event_name;
      inspect::Node& parent_node;
      Reporter& reporter;
      uint32_t cobalt_component_id;
      uint32_t cobalt_event_duration_metric_id;
      uint32_t cobalt_time_since_last_event_or_session_start_metric_id;
    };
    OverflowUnderflowTracker(Args args);

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

    Reporter& reporter_;
    const uint32_t cobalt_component_id_;
    const uint32_t cobalt_event_duration_metric_id_;
    const uint32_t cobalt_time_since_last_event_or_session_start_metric_id_;
  };

  struct Device {
    Device(inspect::Node node);
    inspect::Node node_;
    inspect::DoubleProperty gain_db_;
    inspect::UintProperty muted_;
    inspect::UintProperty agc_supported_;
    inspect::UintProperty agc_enabled_;
  };

  struct Output : public Device {
    Output(inspect::Node, Reporter&);
    std::unique_ptr<OverflowUnderflowTracker> device_underflows_;
    std::unique_ptr<OverflowUnderflowTracker> pipeline_underflows_;
  };

  struct Input : public Device {
    Input(inspect::Node node);
  };

  struct PayloadBuffer {
    PayloadBuffer(inspect::Node node, uint64_t size) : node_(std::move(node)) {
      size_ = node_.CreateUint("size", size);
      packets_ = node_.CreateUint("packets", 0);
    }

    inspect::Node node_;
    inspect::UintProperty size_;
    inspect::UintProperty packets_;
  };

  struct ClientPort {
    ClientPort(inspect::Node node);
    inspect::Node node_;
    inspect::UintProperty sample_format_;
    inspect::UintProperty channels_;
    inspect::UintProperty frames_per_second_;

    inspect::Node payload_buffers_node_;
    std::unordered_map<uint32_t, PayloadBuffer> payload_buffers_;

    inspect::DoubleProperty gain_db_;
    inspect::UintProperty muted_;

    // We're just counting these calls for now. |SetGainWithRamp| isn't
    // implemented and should never be called.
    inspect::UintProperty set_gain_with_ramp_calls_;
  };

  struct Renderer : ClientPort {
    Renderer(inspect::Node, Reporter&);
    inspect::UintProperty min_lead_time_ns_;
    inspect::DoubleProperty pts_continuity_threshold_seconds_;
    inspect::DoubleProperty final_stream_gain_;
    std::unique_ptr<OverflowUnderflowTracker> underflows_;
  };

  struct Capturer : ClientPort {
    Capturer(inspect::Node, Reporter&);
    inspect::UintProperty min_fence_time_ns_;
    std::unique_ptr<OverflowUnderflowTracker> overflows_;
  };

  Output* FindOutput(const AudioDevice& device);
  Input* FindInput(const AudioDevice& device);
  Renderer* FindRenderer(const fuchsia::media::AudioRenderer& renderer);
  Capturer* FindCapturer(const fuchsia::media::AudioCapturer& capturer);
  std::string NextRendererName();
  std::string NextCapturerName();

  sys::ComponentContext& component_context_;
  ThreadingModel& threading_model_;
  std::unique_ptr<sys::ComponentInspector> inspector_;
  inspect::UintProperty failed_to_open_device_count_;
  inspect::UintProperty failed_to_obtain_fdio_service_channel_count_;
  inspect::UintProperty failed_to_obtain_stream_channel_count_;
  inspect::UintProperty device_startup_failed_count_;
  inspect::Node outputs_node_;
  inspect::Node inputs_node_;
  inspect::Node renderers_node_;
  inspect::Node capturers_node_;
  std::unordered_map<const AudioDevice*, Output> outputs_;
  std::unordered_map<const AudioDevice*, Input> inputs_;
  std::unordered_map<const fuchsia::media::AudioRenderer*, Renderer> renderers_;
  std::unordered_map<const fuchsia::media::AudioCapturer*, Capturer> capturers_;
  uint64_t next_renderer_name_ = 0;
  uint64_t next_capturer_name_ = 0;

  // Connection to cobalt to log telemetry
  fuchsia::cobalt::LoggerFactoryPtr cobalt_factory_;
  fuchsia::cobalt::LoggerPtr cobalt_logger_;
};

#define REPORT(x)                                                                        \
  do {                                                                                   \
    auto s = media::audio::Reporter::Singleton();                                        \
    if (!s) {                                                                            \
      FX_LOGS(WARNING) << "Reporting metrics before the Reporter singleton was created"; \
    } else {                                                                             \
      s->x;                                                                              \
    }                                                                                    \
  } while (0)
#else  // ENABLE_REPORTER
#define REPORT(x) (void)0
#endif  // ENABLE_REPORTER

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_
