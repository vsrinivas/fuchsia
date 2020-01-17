// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/media/cpp/timeline_function.h>
#include <zircon/device/audio.h>

#include <memory>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_device_settings.h"
#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/device_registry.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/wakeup_event.h"

namespace media::audio {

class AudioDriver;
class RingBuffer;

class AudioDevice : public AudioObject, public std::enable_shared_from_this<AudioDevice> {
 public:
  // Wakeup
  //
  // Called from outside the mixing ExecutionDomain to cause an AudioDevice's::OnWakeup handler to
  // run from within the context of the mixing execution domain.
  void Wakeup();

  ~AudioDevice() override;

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
  bool plugged() const { return plugged_; }
  zx::time plug_time() const { return plug_time_; }
  AudioDriver* driver() const { return driver_.get(); }
  uint64_t token() const;
  bool activated() const { return activated_; }

  // AudioObjects with Type::Output must override this; this version should never be called.
  virtual zx::duration min_lead_time() const {
    FX_CHECK(false) << "min_lead_time() not supported on AudioDevice";
    return zx::nsec(0);
  }

  // AudioObject overrides.
  std::optional<VolumeCurve> GetVolumeCurve() const override;

  // Accessor set gain. Limits the gain command to what the hardware allows, and
  // wakes up the device in the event of a meaningful change in gain settings.
  //
  // Only called by AudioDeviceManager, and only after the device is activated.
  void SetGainInfo(const fuchsia::media::AudioGainInfo& info, uint32_t set_flags);

  // Device info used during device enumeration and add-notifications.
  void GetDeviceInfo(fuchsia::media::AudioDeviceInfo* out_info) const;

  // Gives derived classes a chance to set up hardware, then sets up the machinery needed for
  // scheduling processing tasks and schedules the first processing callback immediately in order
  // to get the process running.
  fit::promise<void, zx_status_t> Startup();

  // Makes  certain that the shutdown process has started, synchronizes with processing tasks which
  // were executing at the time, then finishes the shutdown by unlinking from all renderers and
  // capturers and cleaning up all resources.
  fit::promise<void> Shutdown();

 protected:
  AudioDevice(Type type, ThreadingModel* threading_model, DeviceRegistry* registry);

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
  virtual void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) = 0;

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

  // AudioDriver accessors.
  const std::shared_ptr<RingBuffer>& driver_ring_buffer() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  const TimelineFunction& device_reference_clock_to_ring_pos_bytes() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  ExecutionDomain& mix_domain() const { return *mix_domain_; }
  ThreadingModel& threading_model() { return threading_model_; }
  DeviceRegistry& device_registry() { return device_registry_; }
  const fbl::RefPtr<AudioDeviceSettings>& device_settings() const { return device_settings_; }

 private:
  DeviceRegistry& device_registry_;
  ThreadingModel& threading_model_;
  ThreadingModel::OwnedDomainPtr mix_domain_;
  WakeupEvent mix_wakeup_;

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
  friend class AudioDriver;
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

  std::atomic<bool> shutting_down_{false};
  volatile bool shut_down_ = false;
  volatile bool activated_ = false;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_H_
