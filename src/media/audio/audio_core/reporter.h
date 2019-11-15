// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/sys/inspect/cpp/component.h>

#include <unordered_map>

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
// caller. Calls to those methods are made using the |REP| macro, which is
// notationally simple:
//
//     REP(ThatThingHappened(details, more_details));
//
// Use of the macro also allows instrumentation to be dropped from the build
// using a simple gn argument, if desired (by setting ENABLE_REPORTER to 0).
// The reporter is provisioned by calling its |Init| method, also using the
// |REP| macro. This is done in main.cc:
//
//    REP(Init(component_context));
//
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
  static Reporter& Singleton();

  const inspect::Inspector& inspector() { return *inspector_->inspector(); }

  ////////////////////////////////////////////////////////////////////////////
  // The following methods are intended to be called using REP. For example:
  //
  //     REP(Init(component_context));
  //
  void Init(sys::ComponentContext* component_context);

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
                             const fuchsia::media::AudioGainInfo& gain_info, uint32_t set_flags);

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

  // Logs an Underflow event to cobalt. output_duration_missed is the amount of time by which we
  // missed a time-critical write into the output buffer.
  void OutputUnderflow(zx::duration output_duration_missed, zx::time uptime_to_underflow);

 private:
  void InitInspect();
  void InitCobalt();

  struct Device {
    Device(inspect::Node node) : node_(std::move(node)) {
      gain_db_ = node_.CreateDouble("gain db", 0.0);
      muted_ = node_.CreateUint("muted", 0);
      agc_supported_ = node_.CreateUint("agc supported", 0);
      agc_enabled_ = node_.CreateUint("agc enabled", 0);
    }
    inspect::Node node_;
    inspect::DoubleProperty gain_db_;
    inspect::UintProperty muted_;
    inspect::UintProperty agc_supported_;
    inspect::UintProperty agc_enabled_;
  };

  struct Output : public Device {
    Output(inspect::Node node) : Device(std::move(node)) {}
  };

  struct Input : public Device {
    Input(inspect::Node node) : Device(std::move(node)) {}
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
    ClientPort(inspect::Node node) : node_(std::move(node)) {
      sample_format_ = node_.CreateUint("sample format", 0);
      channels_ = node_.CreateUint("channels", 0);
      frames_per_second_ = node_.CreateUint("frames per second", 0);
      payload_buffers_node_ = node_.CreateChild("payload buffers");
      gain_db_ = node_.CreateDouble("gain db", 0.0);
      muted_ = node_.CreateUint("muted", 0);
      set_gain_with_ramp_calls_ = node_.CreateUint("calls to SetGainWithRamp", 0);
    }
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
    Renderer(inspect::Node node) : ClientPort(std::move(node)) {
      min_lead_time_ns_ = node_.CreateUint("min lead time (ns)", 0);
      pts_continuity_threshold_seconds_ = node_.CreateDouble("pts continuity threshold (s)", 0.0);
    }
    inspect::UintProperty min_lead_time_ns_;
    inspect::DoubleProperty pts_continuity_threshold_seconds_;
  };

  struct Capturer : ClientPort {
    Capturer(inspect::Node node) : ClientPort(std::move(node)) {
      min_fence_time_ns_ = node_.CreateUint("min fence time (ns)", 0);
    }
    inspect::UintProperty min_fence_time_ns_;
  };

  Device* FindOutput(const AudioDevice& device);
  Device* FindInput(const AudioDevice& device);
  Renderer* FindRenderer(const fuchsia::media::AudioRenderer& renderer);
  Capturer* FindCapturer(const fuchsia::media::AudioCapturer& capturer);
  std::string NextRendererName();
  std::string NextCapturerName();

  sys::ComponentContext* component_context_ = nullptr;
  std::shared_ptr<sys::ComponentInspector> inspector_;
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

#define REP(x) media::audio::Reporter::Singleton().x
#else  // ENABLE_REPORTER
#define REP(x) (void)0
#endif  // ENABLE_REPORTER

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_
