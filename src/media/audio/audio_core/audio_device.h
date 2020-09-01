// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <zircon/device/audio.h>

#include <memory>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/audio_device_settings.h"
#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/device_config.h"
#include "src/media/audio/audio_core/device_registry.h"
#include "src/media/audio/audio_core/link_matrix.h"
#include "src/media/audio/audio_core/pipeline_config.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/wakeup_event.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class AudioDriver;
class ReadableRingBuffer;
class WritableRingBuffer;

class AudioDevice : public AudioObject, public std::enable_shared_from_this<AudioDevice> {
 public:
  static std::string UniqueIdToString(const audio_stream_unique_id_t& id);
  static fit::result<audio_stream_unique_id_t> UniqueIdFromString(const std::string& unique_id);

  ~AudioDevice() override;

  const std::string& name() const { return name_; }

  // Wakeup
  //
  // Called from outside the mixing ExecutionDomain to cause an AudioDevice's::OnWakeup handler to
  // run from within the context of the mixing execution domain.
  void Wakeup();

  // |media::audio::AudioObject|
  std::optional<Format> format() const override;

  // Accessors for the current plug state of the device.
  //
  // In addition to publishing and unpublishing streams when codecs are attached to or removed from
  // hot-pluggable buses (such as USB), some codecs have the ability to detect the plugged/unplugged
  // state of external connectors (such as a 3.5mm audio jack). Drivers can report this state as
  // well as the time of the last state change. Currently this information is used in the Audio
  // Service to implement simple routing policies for AudioRenderers and AudioCapturers.
  //
  // plugged   : true when an audio output/input stream is either hardwired, or
  //             believes that it has something connected to its plug.
  // plug_time : The time (per zx::clock::get_monotonic() at which the plugged/unplugged state of
  //             this output or input last changed.
  // routable  : False when a device's OutputPipeline is being updated in AudioDeviceManager. This
  //             protects a device from being plugged or unplugged while undergoing the
  //             PipelineConfig update, as well as ensures only one PipelineConfig update is applied
  //             at a time.
  bool plugged() const { return plugged_; }
  zx::time plug_time() const { return plug_time_; }
  bool routable() const { return routable_; }
  AudioDriver* driver() const { return driver_.get(); }
  uint64_t token() const;
  bool activated() const { return activated_; }

  const DeviceConfig::DeviceProfile& profile() const;
  const DeviceConfig& config() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {
    return config_;
  }
  // `set_config()` strictly updates the |config_| variable returned from `config()`; it does not
  // rebuild the OutputPipeline. To restart the OutputPipeline with an updated configuration, see
  // `UpdateDeviceProfile()`.
  void set_config(const DeviceConfig& config) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {
    config_ = config;
  }

  // Presentation delay for this device.
  zx::duration presentation_delay() const { return presentation_delay_; }

  // Sets the configuration of all effects with the given instance name.
  virtual fit::promise<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) {
    return fit::make_error_promise(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
  }

  // AudioObjects with Type::Output must override this; this version should never be called.
  //
  // UpdateDeviceProfile differs from `set_config()` in two ways:
  // 1. It explicitly updates the OutputPipeline with a new OutputDeviceProfile configuration,
  //    restarting the new OutputPipeline with the updated configuration.
  // 2. It provides a convenient way to update the configuration outside of the mixer thread.
  virtual fit::promise<void, zx_status_t> UpdateDeviceProfile(
      const DeviceConfig::OutputDeviceProfile::Parameters& params) {
    FX_CHECK(false) << "UpdateDeviceProfile() not supported on AudioDevice";
    return fit::make_error_promise(ZX_ERR_NOT_SUPPORTED);
  }

  // Accessor set gain. Limits the gain command to what the hardware allows, and
  // wakes up the device in the event of a meaningful change in gain settings.
  //
  // Only called by AudioDeviceManager, and only after the device is activated.
  virtual void SetGainInfo(const fuchsia::media::AudioGainInfo& info,
                           fuchsia::media::AudioGainValidFlags set_flags);

  // Device info used during device enumeration and add-notifications.
  virtual fuchsia::media::AudioDeviceInfo GetDeviceInfo() const;

  // Gives derived classes a chance to set up hardware, then sets up the machinery needed for
  // scheduling processing tasks and schedules the first processing callback immediately in order
  // to get the process running.
  virtual fit::promise<void, zx_status_t> Startup();

  // Makes certain that the shutdown process has started, synchronizes with processing tasks which
  // were executing at the time, then finishes the shutdown by unlinking from all renderers and
  // capturers and cleaning up all resources.
  virtual fit::promise<void> Shutdown();

  // audio clock from AudioDriver
  AudioClock& reference_clock();

 protected:
  AudioDevice(Type type, const std::string& name, ThreadingModel* threading_model,
              DeviceRegistry* registry, LinkMatrix* link_matrix,
              std::unique_ptr<AudioDriver> driver);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Methods which may be implemented by derived classes to customize behavior.
  //
  //////////////////////////////////////////////////////////////////////////////

  // Init
  //
  // Called during startup on the mixer thread. Derived classes should begin the
  // process of driver initialization at this point. Return ZX_OK if things have
  // started and we are waiting for driver init.
  virtual zx_status_t Init() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Cleanup
  //
  // Called at shutdown on the mixer thread to allow derived classes to clean
  // up any allocated resources. All other audio objects have been
  // disconnected/unlinked. No locks are being held.
  virtual void Cleanup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // ApplyGainLimits
  //
  // Modify the contents of a user request to change the gain state to reflect
  // the actual gain that we are going to end up setting.  This may differ from
  // the requested gain due to hardware limitations or general policy.
  virtual void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                               fuchsia::media::AudioGainValidFlags set_flags) = 0;

  //////////////////////////////////////////////////////////////////////////////
  //
  // Methods which may used by derived classes from within the context of a
  // mix_domain_ ExecutionDomain.  Note; since these methods are intended to be
  // called from the within the mix_domain_, callers must be annotated properly
  // to demonstrate that they are executing from within that domain.
  //

  // OnWakeup
  //
  // Called in response to someone from outside the domain poking the
  // mix_wakeup_ WakeupEvent.  At a minimum, the framework will call this once
  // at startup to get the output running.
  virtual void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;

  // ActivateSelf
  //
  // Send a message to the audio device manager to let it know that we are ready
  // to be added to the set of active devices.
  void ActivateSelf() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // ShutdownSelf
  //
  // Kick off the process of shutting ourselves down. Note: after this method is
  // called, no new callbacks may be scheduled. When the main message loop gets
  // our shutdown request, it completes the process by unlinking us from our
  // AudioRenderers/AudioCapturers and calling our Cleanup.
  void ShutdownSelf() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Check the shutting down flag.  We are in the process of shutting down when
  // we have become deactivated at the dispatcher framework level.
  inline bool is_shutting_down() const { return shutting_down_.load(); }

  //////////////////////////////////////////////////////////////////////////////
  //
  // AudioDriver hooks.
  //
  // Hooks used by encapsulated AudioDriver instances to notify AudioDevices
  // about internal state machine changes.
  virtual void OnDriverInfoFetched() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {}

  virtual void OnDriverConfigComplete() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {}

  virtual void OnDriverStartComplete() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {}

  virtual void OnDriverStopComplete() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {}

  virtual void OnDriverPlugStateChange(bool plugged, zx::time plug_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {
    TRACE_DURATION("audio", "AudioDevice::OnDriverPlugStateChange");
    threading_model().FidlDomain().PostTask([output = shared_from_this(), plugged, plug_time]() {
      output->device_registry().OnPlugStateChanged(std::move(output), plugged, plug_time);
    });
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // Other methods.
  //

  // UpdatePlugState
  //
  // Called by the audio device manager on the main message loop when it is
  // notified of a plug state change for a device. Used to update the internal
  // bookkeeping about the current plugged/unplugged state. This method may also
  // be used by derived classes during Init to set an initial plug state.
  //
  // Returns true if the plug state has changed, or false otherwise.
  bool UpdatePlugState(bool plugged, zx::time plug_time);

  // UpdateRoutableState
  //
  // Called by the audio device manager on the main message loop when the device is
  // undergoing an update to its OutputPipeline. Used to update the internal
  // bookkeeping about the current routable/unroutable state.
  void UpdateRoutableState(bool routable);

  // AudioDriver accessors.
  const std::shared_ptr<ReadableRingBuffer>& driver_readable_ring_buffer() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  const std::shared_ptr<WritableRingBuffer>& driver_writable_ring_buffer() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Accessors for some of the useful timeline functions computed by the driver
  // after streaming starts.
  //
  // Maps from a presentation/capture time on the reference clock to fractional
  // frame number in the stream.  The presentation/capture time refers to the
  // time that the sound either exits the speaker or enters the microphone.
  virtual const TimelineFunction& driver_ref_time_to_frac_presentation_frame() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Maps from a time on the reference clock to the safe read/write frame number
  // in the stream.  The safe read/write pointer is the point in the stream
  // which is one fifo_depth away from the interconnect entry/exit point in the
  // stream.  This is the point at which an input stream is guaranteed to have
  // moved its captured data from the hardware into RAM, or the point at which
  // an output stream may have already moved data to be transmitted from RAM
  // into the hardware.  When consuming or producing audio from an input or
  // output stream, users must always stay ahead of this point.
  virtual const TimelineFunction& driver_ref_time_to_frac_safe_read_or_write_frame() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Update the presentation delay for this device (defaults to zero).
  void SetPresentationDelay(zx::duration delay) { presentation_delay_ = delay; }

  ExecutionDomain& mix_domain() const { return *mix_domain_; }
  ThreadingModel& threading_model() { return threading_model_; }
  DeviceRegistry& device_registry() { return device_registry_; }
  const fbl::RefPtr<AudioDeviceSettings>& device_settings() const { return device_settings_; }

 private:
  const std::string name_;
  DeviceRegistry& device_registry_;
  ThreadingModel& threading_model_;
  ThreadingModel::OwnedDomainPtr mix_domain_;
  WakeupEvent mix_wakeup_;

  DeviceConfig config_ = ProcessConfig::instance().device_config();

  // This object manages most interactions with the low-level driver for us.
  std::unique_ptr<AudioDriver> driver_;

  // Persistable settings.  Note, this is instantiated by the audio device
  // itself during activate self so that it may be pre-populated with the
  // current hardware state, and so the presence/absence of this pointer is
  // always coherent from the view of the mix_domain.  Once instantiated, this
  // class lives for as long as the AudioDevice does.
  fbl::RefPtr<AudioDeviceSettings> device_settings_;

  // It's always nice when you manager is also your friend.  Seriously though,
  // the AudioDeviceManager gets to call Startup and Shutdown, no one else
  // (including derived classes) should be able to.
  friend class AudioDeviceManager;
  friend class AudioDriverV1;
  friend class AudioDriverV2;
  friend struct PendingInitListTraits;

  // Called from the AudioDeviceManager when it moves an audio device from its
  // "pending init" set over to its "active" set .
  void SetActivated() {
    FX_DCHECK(!activated());
    activated_ = true;
  }

  bool system_gain_dirty = true;

  // Plug state is protected by the fact that it is only ever accessed on the
  // main message loop thread.
  bool plugged_ = false;
  zx::time plug_time_;

  // Routable state is protected by the fact that it is only ever accessed on the
  // main message loop thread.
  bool routable_ = true;

  std::atomic<bool> shutting_down_{false};
  volatile bool shut_down_ = false;
  volatile bool activated_ = false;

  LinkMatrix& link_matrix_;
  zx::duration presentation_delay_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_H_
