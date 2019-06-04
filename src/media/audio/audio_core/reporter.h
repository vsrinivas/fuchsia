// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/inspect/component.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#ifndef ENABLE_REPORTER
#define ENABLE_REPORTER 1
#endif

namespace media::audio {

#if ENABLE_REPORTER

class AudioDevice;
class AudioRendererImpl;
class AudioCapturerImpl;

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
// TODO(dalesat): Add cobalt logging.
class Reporter {
 public:
  static Reporter& Singleton();

  const inspect::Tree& tree() { return *inspector_->root_tree(); }

  ////////////////////////////////////////////////////////////////////////////
  // The following methods are intended to be called using REP. For example:
  //
  //     REP(Init(component_context));
  //
  void Init(sys::ComponentContext* component_context);

  // Devices.
  void FailedToOpenDevice(const std::string& name, bool is_input, int err);
  void FailedToObtainFdioServiceChannel(const std::string& name, bool is_input,
                                        zx_status_t status);
  void FailedToObtainStreamChannel(const std::string& name, bool is_input,
                                   zx_status_t status);
  void AddingDevice(const std::string& name, const AudioDevice& device);
  void RemovingDevice(const AudioDevice& device);
  void DeviceStartupFailed(const AudioDevice& device);
  void IgnoringDevice(const AudioDevice& device);
  void ActivatingDevice(const AudioDevice& device);
  void SettingDeviceGainInfo(const AudioDevice& device,
                             const fuchsia::media::AudioGainInfo& gain_info,
                             uint32_t set_flags);

  // Renderers.
  void AddingRenderer(const AudioRendererImpl& renderer);
  void RemovingRenderer(const AudioRendererImpl& renderer);
  void SettingRendererStreamType(
      const AudioRendererImpl& renderer,
      const fuchsia::media::AudioStreamType& stream_type);
  void AddingRendererPayloadBuffer(const AudioRendererImpl& renderer,
                                   uint32_t buffer_id, uint64_t size);
  void SettingRendererGain(const AudioRendererImpl& renderer, float gain_db);
  void SettingRendererGainWithRamp(const AudioRendererImpl& renderer,
                                   float gain_db, zx_duration_t duration_ns,
                                   fuchsia::media::audio::RampType ramp_type);
  void SettingRendererMute(const AudioRendererImpl& renderer, bool muted);
  void SettingRendererMinClockLeadTime(const AudioRendererImpl& renderer,
                                       int64_t min_clock_lead_time_ns);
  void SettingRendererPtsContinuityThreshold(const AudioRendererImpl& renderer,
                                             float threshold_seconds);

  // Capturers.
  void AddingCapturer(const AudioCapturerImpl& capturer);
  void RemovingCapturer(const AudioCapturerImpl& capturer);
  void SettingCapturerStreamType(
      const AudioCapturerImpl& capturer,
      const fuchsia::media::AudioStreamType& stream_type);
  void AddingCapturerPayloadBuffer(const AudioCapturerImpl& capturer,
                                   uint32_t buffer_id, uint64_t size);
  void SettingCapturerGain(const AudioCapturerImpl& capturer, float gain_db);
  void SettingCapturerGainWithRamp(const AudioCapturerImpl& capturer,
                                   float gain_db, zx_duration_t duration_ns,
                                   fuchsia::media::audio::RampType ramp_type);
  void SettingCapturerMute(const AudioCapturerImpl& capturer, bool muted);

 private:
  struct Device {
    Device(inspect::Node node) : node_(std::move(node)) {
      gain_db_ = node_.CreateDoubleMetric("gain db", 0.0);
      muted_ = node_.CreateUIntMetric("muted", 0);
      agc_supported_ = node_.CreateUIntMetric("agc supported", 0);
      agc_enabled_ = node_.CreateUIntMetric("agc enabled", 0);
    }
    inspect::Node node_;
    inspect::DoubleMetric gain_db_;
    inspect::UIntMetric muted_;
    inspect::UIntMetric agc_supported_;
    inspect::UIntMetric agc_enabled_;
  };

  struct Output : public Device {
    Output(inspect::Node node) : Device(std::move(node)) {}
  };

  struct Input : public Device {
    Input(inspect::Node node) : Device(std::move(node)) {}
  };

  struct ClientPort {
    ClientPort(inspect::Node node) : node_(std::move(node)) {
      sample_format_ = node_.CreateUIntMetric("sample format", 0);
      channels_ = node_.CreateUIntMetric("channels", 0);
      frames_per_second_ = node_.CreateUIntMetric("frames per second", 0);
      payload_buffer_size_ = node_.CreateUIntMetric("payload buffer size", 0);
      gain_db_ = node_.CreateDoubleMetric("gain db", 0.0);
      muted_ = node_.CreateUIntMetric("muted", 0);
      set_gain_with_ramp_calls_ =
          node_.CreateUIntMetric("calls to SetGainWithRamp", 0);
    }
    inspect::Node node_;
    inspect::UIntMetric sample_format_;
    inspect::UIntMetric channels_;
    inspect::UIntMetric frames_per_second_;

    // We only support one payload buffer in a renderer or capturer, so this
    // single value is adequate for now. When multiple buffers are supported,
    // we'll need a collection of id/size pairs.
    inspect::UIntMetric payload_buffer_size_;

    inspect::DoubleMetric gain_db_;
    inspect::UIntMetric muted_;

    // We're just counting these calls for now. |SetGainWithRamp| isn't
    // implemented and should never be called.
    inspect::UIntMetric set_gain_with_ramp_calls_;
  };

  struct Renderer : ClientPort {
    Renderer(inspect::Node node) : ClientPort(std::move(node)) {
      min_clock_lead_time_ns_ =
          node_.CreateUIntMetric("min clock lead time (ns)", 0);
      pts_continuity_threshold_seconds_ =
          node_.CreateDoubleMetric("pts continuity threshold (s)", 0.0);
    }
    inspect::UIntMetric min_clock_lead_time_ns_;
    inspect::DoubleMetric pts_continuity_threshold_seconds_;
  };

  struct Capturer : ClientPort {
    Capturer(inspect::Node node) : ClientPort(std::move(node)) {}
  };

  Device* FindOutput(const AudioDevice& device);
  Device* FindInput(const AudioDevice& device);
  Renderer* FindRenderer(const AudioRendererImpl& renderer);
  Capturer* FindCapturer(const AudioCapturerImpl& capturer);
  std::string NextRendererName();
  std::string NextCapturerName();

  sys::ComponentContext* component_context_ = nullptr;
  std::shared_ptr<inspect::ComponentInspector> inspector_;
  inspect::UIntMetric failed_to_open_device_count_;
  inspect::UIntMetric failed_to_obtain_fdio_service_channel_count_;
  inspect::UIntMetric failed_to_obtain_stream_channel_count_;
  inspect::UIntMetric device_startup_failed_count_;
  inspect::Node outputs_node_;
  inspect::Node inputs_node_;
  inspect::Node renderers_node_;
  inspect::Node capturers_node_;
  std::unordered_map<const AudioDevice*, Output> outputs_;
  std::unordered_map<const AudioDevice*, Input> inputs_;
  std::unordered_map<const AudioRendererImpl*, Renderer> renderers_;
  std::unordered_map<const AudioCapturerImpl*, Capturer> capturers_;
  uint64_t next_renderer_name_ = 0;
  uint64_t next_capturer_name_ = 0;
};

#define REP(x) media::audio::Reporter::Singleton().x
#else  // ENABLE_REPORTER
#define REP(x) (void)0
#endif  // ENABLE_REPORTER

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_
