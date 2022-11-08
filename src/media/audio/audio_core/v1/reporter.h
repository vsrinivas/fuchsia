// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_REPORTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_REPORTER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/inspect/cpp/component.h>

#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <unordered_map>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/shared/metrics/metrics_impl.h"
#include "src/media/audio/audio_core/shared/stream_usage.h"
#include "src/media/audio/audio_core/v1/audio_admin.h"
#include "src/media/audio/audio_core/v1/threading_model.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

class AudioDriver;

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
                                  ThreadingModel& threading_model, bool enable_cobalt);

  class Device {
   public:
    virtual ~Device() = default;

    virtual void Destroy() = 0;

    virtual void StartSession(zx::time start_time) = 0;
    virtual void StopSession(zx::time stop_time) = 0;

    virtual void SetDriverInfo(const AudioDriver& driver) = 0;
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

    virtual void Destroy() = 0;

    virtual void StartSession(zx::time start_time) = 0;
    virtual void StopSession(zx::time stop_time) = 0;

    virtual void SetUsage(RenderUsage usage) = 0;
    virtual void SetFormat(const Format& format) = 0;
    virtual void SetGain(float gain_db) = 0;
    virtual void SetGainWithRamp(float gain_db, zx::duration duration,
                                 fuchsia::media::audio::RampType ramp_type) = 0;
    virtual void SetFinalGain(float gain_db) = 0;
    virtual void SetMute(bool muted) = 0;
    virtual void SetMinLeadTime(zx::duration min_lead_time) = 0;
    virtual void SetPtsContinuityThreshold(float threshold_seconds) = 0;
    virtual void SetPtsUnits(uint32_t numerator, uint32_t denominator) = 0;

    virtual void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) = 0;
    virtual void RemovePayloadBuffer(uint32_t buffer_id) = 0;
    virtual void SendPacket(const fuchsia::media::StreamPacket& packet) = 0;
    virtual void Underflow(zx::time start_time, zx::time end_time) = 0;
  };

  class Capturer {
   public:
    virtual ~Capturer() = default;

    virtual void Destroy() = 0;

    virtual void StartSession(zx::time start_time) = 0;
    virtual void StopSession(zx::time stop_time) = 0;

    virtual void SetUsage(CaptureUsage usage) = 0;
    virtual void SetFormat(const Format& format) = 0;
    virtual void SetGain(float gain_db) = 0;
    virtual void SetGainWithRamp(float gain_db, zx::duration duration,
                                 fuchsia::media::audio::RampType ramp_type) = 0;
    virtual void SetMute(bool muted) = 0;
    virtual void SetMinFenceTime(zx::duration min_fence_time) = 0;

    virtual void AddPayloadBuffer(uint32_t buffer_id, uint64_t size) = 0;
    virtual void SendPacket(const fuchsia::media::StreamPacket& packet) = 0;
    virtual void Overflow(zx::time start_time, zx::time end_time) = 0;
  };

  class VolumeControl {
   public:
    virtual ~VolumeControl() = default;

    virtual void Destroy() = 0;

    virtual void SetVolumeMute(float volume, bool mute) = 0;
    virtual void AddBinding(std::string name) = 0;
  };

  // This class is an implementation detail.
  // Container::Ptr is a smart pointer that calls T::Destroy() when the Ptr is destructed.
  // The underlying object may be cached for some time afterwards.
  // ObjectsToCache is the number of destroyed objects to cache, in addition to the
  // current alive object.
  template <typename T, size_t ObjectsToCache>
  class Container {
   public:
    class Ptr {
     public:
      Ptr(Container<T, ObjectsToCache>* c, std::shared_ptr<T> p) : container_(c), ptr_(p) {}
      Ptr(const Ptr&) = delete;
      Ptr(Ptr&&) = default;
      ~Ptr() { Drop(); }

      Ptr& operator=(Ptr&& rhs) noexcept {
        Drop();
        ptr_ = std::move(rhs.ptr_);
        container_ = rhs.container_;
        rhs.container_ = nullptr;
        return *this;
      }

      T& operator*() const { return *ptr_; }
      T* operator->() const { return ptr_.get(); }

      void Drop() {
        if (ptr_) {
          ptr_->Destroy();
          container_->Kill(ptr_);
          ptr_ = nullptr;
        }
      }

     private:
      Container<T, ObjectsToCache>* container_ = nullptr;
      std::shared_ptr<T> ptr_;
    };

   private:
    friend class Reporter;
    friend class Ptr;

    Ptr New(T* object) {
      std::shared_ptr<T> ptr(object);
      std::lock_guard<std::mutex> lock(mutex_);
      alive_.insert(ptr);
      return Ptr(this, ptr);
    }

    void Kill(const std::shared_ptr<T>& ptr) {
      std::lock_guard<std::mutex> lock(mutex_);
      alive_.erase(ptr);
      while (dead_.size() >= ObjectsToCache) {
        dead_.pop();
      }
      dead_.push(ptr);
    }

    std::mutex mutex_;
    std::set<std::shared_ptr<T>> alive_ FXL_GUARDED_BY(mutex_);
    std::queue<std::shared_ptr<T>> dead_ FXL_GUARDED_BY(mutex_);
  };

  Reporter() {}
  Reporter(sys::ComponentContext& component_context, ThreadingModel& threading_model,
           bool enable_cobalt);

  static constexpr size_t kObjectsToCache = 4;
  static constexpr size_t kVolumeControlsToCache = 10;
  static constexpr size_t kActiveUsagePoliciesToCache = 10;

  Container<OutputDevice, kObjectsToCache>::Ptr CreateOutputDevice(const std::string& name,
                                                                   const std::string& thread_name);
  Container<InputDevice, kObjectsToCache>::Ptr CreateInputDevice(const std::string& name,
                                                                 const std::string& thread_name);
  Container<Renderer, kObjectsToCache>::Ptr CreateRenderer();
  Container<Capturer, kObjectsToCache>::Ptr CreateCapturer(const std::string& thread_name);
  Container<VolumeControl, kVolumeControlsToCache>::Ptr CreateVolumeControl();

  // Thermal state of Audio system.
  void SetNumThermalStates(size_t num);
  void SetThermalState(uint32_t state);

  // Audio policy logging of usage activity and behavior (none|duck|mute).
  void SetAudioPolicyBehaviorGain(AudioAdmin::BehaviorGain behavior_gain);
  void UpdateActiveUsagePolicy(const std::vector<fuchsia::media::Usage>& active_usages,
                               const AudioAdmin::RendererPolicies& renderer_policies,
                               const AudioAdmin::CapturerPolicies& capturer_policies);

  // Device creation failures.
  void FailedToOpenDevice(const std::string& name, bool is_input, int err);
  void FailedToObtainFdioServiceChannel(const std::string& name, bool is_input, zx_status_t status);
  void FailedToObtainStreamChannel(const std::string& name, bool is_input, zx_status_t status);
  void FailedToStartDevice(const std::string& name);

  // Mixer events which are not easily tied to a specific device or client.
  void MixerClockSkewDiscontinuity(zx::duration abs_clock_error);

  // Exported for tests.
  const inspect::Inspector& inspector() {
    std::lock_guard<std::mutex> lock(mutex_);
    return *impl_->inspector->inspector();
  }

 private:
  static constexpr size_t kThermalStatesToCache = 8;

  class OverflowUnderflowTracker;
  class ObjectTracker;
  class DeviceDriverInfo;
  class ThermalStateTransition;
  class ThermalStateTracker;
  class OutputDeviceImpl;
  class InputDeviceImpl;
  class ClientPort;
  class RendererImpl;
  class CapturerImpl;
  class VolumeControlImpl;
  class VolumeSetting;
  class ActiveUsagePolicy;
  class ActiveUsagePolicyTracker;
  struct Impl;

  friend class OverflowUnderflowTracker;
  friend class ObjectTracker;
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
    std::unique_ptr<media::audio::MetricsImpl> metrics_impl;

    inspect::UintProperty failed_to_open_device_count;
    inspect::UintProperty failed_to_obtain_fdio_service_channel_count;
    inspect::UintProperty failed_to_obtain_stream_channel_count;
    inspect::UintProperty failed_to_start_device_count;
    inspect::LinearIntHistogram mixer_clock_skew_discontinuities;
    inspect::Node outputs_node;
    inspect::Node inputs_node;
    inspect::Node renderers_node;
    inspect::Node capturers_node;
    inspect::Node thermal_state_transitions_node;
    inspect::Node volume_controls_node;

    std::unique_ptr<ThermalStateTracker> thermal_state_tracker;
    std::unique_ptr<ActiveUsagePolicyTracker> active_usage_policy_tracker;

    // These could be guarded by Reporter::mutex_, but clang's thread safety
    // analysis cannot represent that relationship.
    std::mutex mutex;
    uint64_t next_renderer_name FXL_GUARDED_BY(mutex) = 0;
    uint64_t next_capturer_name FXL_GUARDED_BY(mutex) = 0;
    uint64_t next_thermal_transition_name FXL_GUARDED_BY(mutex) = 0;
    uint64_t next_volume_control_name FXL_GUARDED_BY(mutex) = 0;

    Impl(sys::ComponentContext& cc, ThreadingModel& tm);
    ~Impl();

    std::string NextRendererName() {
      std::lock_guard<std::mutex> lock(mutex);
      return std::to_string(++next_renderer_name);
    }
    std::string NextCapturerName() {
      std::lock_guard<std::mutex> lock(mutex);
      return std::to_string(++next_capturer_name);
    }
    std::string NextThermalTransitionName() {
      std::lock_guard<std::mutex> lock(mutex);
      return std::to_string(++next_thermal_transition_name);
    }
    std::string NextVolumeControlName() {
      std::lock_guard<std::mutex> lock(mutex);
      return std::to_string(++next_volume_control_name);
    }
  };

  std::mutex mutex_;
  std::unique_ptr<Impl> impl_ FXL_GUARDED_BY(mutex_);

  // Caches of allocated objects so they can live beyond destruction.
  Container<OutputDevice, kObjectsToCache> outputs_;
  Container<InputDevice, kObjectsToCache> inputs_;
  Container<Renderer, kObjectsToCache> renderers_;
  Container<Capturer, kObjectsToCache> capturers_;
  Container<ThermalStateTransition, kThermalStatesToCache> thermal_state_transitions_;
  Container<VolumeControl, kVolumeControlsToCache> volume_controls_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_REPORTER_H_
