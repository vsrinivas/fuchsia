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
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/audio_core/threading_model.h"

namespace media::audio {

// A singleton instance of |Reporter| handles instrumentation concerns (e.g.
// exposing information via inspect, cobalt, etc) for an audio_core instance.
// The idea is to make instrumentation as simple as possible for the code that
// does the real work. The singleton can be accessed via
//
//   Reporter::Singleton()
//
// Given a Reporter, reporting objects can be created through the Create*()
// methods. Each reporting object is intended to mirror a single object within
// audio_core, such as an AudioRenderer -- the reporting object should live
// exactly as long as its parent audio_core object. In addition to Create*()
// methods, there are FailedTo*() methods that report when an object could not
// be created.
//
// The singleton object always exists: it does not need to be created. However,
// the singleton needs to be initialized, via Reporter::InitializeSingleton().
// Before that static method is called, all reporting objects created by the
// singleton will be no-ops.
//
// The lifetime of each reporting object is divided into sessions. Roughly
// speaking, a session corresponds to a contiguous time spent processing audio.
// For example, for an AudioRenderer, this is the time between Play and Pause events.
// Session lifetimes are controlled by StartSession and StopSession methods.
//
// All times are relative to the system monotonic clock.
//
// This class is fully thread safe, including all static methods and all methods
// on reporting objects.
//
class Reporter {
 public:
  static Reporter& Singleton();
  static void InitializeSingleton(sys::ComponentContext& component_context,
                                  ThreadingModel& threading_model);

  class Device {
   public:
    virtual ~Device() = default;

    virtual void StartSession(zx::time start_time) = 0;
    virtual void StopSession(zx::time stop_time) = 0;

    virtual void SetDriverName(const std::string& driver_name) = 0;
    virtual void SetGainInfo(const fuchsia::media::AudioGainInfo& gain_info,
                             fuchsia::media::AudioGainValidFlags set_flags) = 0;
  };

  class OutputDevice : public Device {
   public:
    virtual void DeviceUnderflow(zx::time start_time, zx::time end_time) = 0;
    virtual void PipelineUnderflow(zx::time start_time, zx::time end_time) = 0;
  };

  class InputDevice : public Device {};

  class Renderer {
   public:
    virtual ~Renderer() = default;

    virtual void StartSession(zx::time start_time) = 0;
    virtual void StopSession(zx::time stop_time) = 0;

    virtual void SetUsage(RenderUsage usage) = 0;
    virtual void SetStreamType(const fuchsia::media::AudioStreamType& stream_type) = 0;
    virtual void SetGain(float gain_db) = 0;
    virtual void SetGainWithRamp(float gain_db, zx::duration duration,
                                 fuchsia::media::audio::RampType ramp_type) = 0;
    virtual void SetFinalGain(float gain_db) = 0;
    virtual void SetMute(bool muted) = 0;
    virtual void SetMinLeadTime(zx::duration min_lead_time) = 0;
    virtual void SetPtsContinuityThreshold(float threshold_seconds) = 0;

    virtual void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) = 0;
    virtual void RemovePayloadBuffer(uint32_t buffer_id) = 0;
    virtual void SendPacket(const fuchsia::media::StreamPacket& packet) = 0;
    virtual void Underflow(zx::time start_time, zx::time end_time) = 0;
  };

  class Capturer {
   public:
    virtual ~Capturer() = default;

    virtual void StartSession(zx::time start_time) = 0;
    virtual void StopSession(zx::time stop_time) = 0;

    virtual void SetUsage(CaptureUsage usage) = 0;
    virtual void SetStreamType(const fuchsia::media::AudioStreamType& stream_type) = 0;
    virtual void SetGain(float gain_db) = 0;
    virtual void SetGainWithRamp(float gain_db, zx::duration duration,
                                 fuchsia::media::audio::RampType ramp_type) = 0;
    virtual void SetMute(bool muted) = 0;
    virtual void SetMinFenceTime(zx::duration min_fence_time) = 0;

    virtual void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) = 0;
    virtual void SendPacket(const fuchsia::media::StreamPacket& packet) = 0;
    virtual void Overflow(zx::time start_time, zx::time end_time) = 0;
  };

  Reporter() {}
  Reporter(sys::ComponentContext& component_context, ThreadingModel& threading_model);

  std::unique_ptr<OutputDevice> CreateOutputDevice(const std::string& name);
  std::unique_ptr<InputDevice> CreateInputDevice(const std::string& name);
  std::unique_ptr<Renderer> CreateRenderer();
  std::unique_ptr<Capturer> CreateCapturer();

  // Device creation failures.
  void FailedToOpenDevice(const std::string& name, bool is_input, int err);
  void FailedToObtainFdioServiceChannel(const std::string& name, bool is_input, zx_status_t status);
  void FailedToObtainStreamChannel(const std::string& name, bool is_input, zx_status_t status);
  void FailedToStartDevice(const std::string& name);

  // Exported for tests.
  const inspect::Inspector& inspector() {
    std::lock_guard<std::mutex> lock(mutex_);
    return *impl_->inspector->inspector();
  }

 private:
  class OverflowUnderflowTracker;
  class OutputDeviceImpl;
  class InputDeviceImpl;
  class RendererImpl;
  class CapturerImpl;

  friend class OverflowUnderflowTracker;
  friend class OutputDeviceImpl;
  friend class InputDeviceImpl;
  friend class RendererImpl;
  friend class CapturerImpl;

  void InitInspect() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void InitCobalt() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // This object contains internal state shared by multiple reporting objects.
  struct Impl {
    sys::ComponentContext& component_context;
    ThreadingModel& threading_model;
    std::unique_ptr<sys::ComponentInspector> inspector;

    fuchsia::cobalt::LoggerFactoryPtr cobalt_factory;
    fuchsia::cobalt::LoggerPtr cobalt_logger;

    inspect::UintProperty failed_to_open_device_count;
    inspect::UintProperty failed_to_obtain_fdio_service_channel_count;
    inspect::UintProperty failed_to_obtain_stream_channel_count;
    inspect::UintProperty failed_to_start_device_count;
    inspect::Node outputs_node;
    inspect::Node inputs_node;
    inspect::Node renderers_node;
    inspect::Node capturers_node;

    // These could be guarded by Reporter::mutex_, but clang's thread safety
    // analysis cannot represent that relationship.
    std::mutex mutex;
    uint64_t next_renderer_name FXL_GUARDED_BY(mutex) = 0;
    uint64_t next_capturer_name FXL_GUARDED_BY(mutex) = 0;

    Impl(sys::ComponentContext& cc, ThreadingModel& tm)
        : component_context(cc), threading_model(tm) {}

    std::string NextRendererName() {
      std::lock_guard<std::mutex> lock(mutex);
      return std::to_string(++next_renderer_name);
    }
    std::string NextCapturerName() {
      std::lock_guard<std::mutex> lock(mutex);
      return std::to_string(++next_capturer_name);
    }
  };

  std::mutex mutex_;
  std::unique_ptr<Impl> impl_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_REPORTER_H_
