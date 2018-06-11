// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_H_

#include <deque>
#include <memory>
#include <set>
#include <thread>

#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-wakeup-event.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/audio_pipe.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/time/time_point.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {
namespace audio {

class AudioDriver;
class DriverRingBuffer;

class AudioDevice : public AudioObject,
                    public fbl::WAVLTreeContainable<fbl::RefPtr<AudioDevice>> {
 public:
  // Wakeup
  //
  // Called from outside the mixing ExecutionDomain to cause an
  // AudioDevice's::OnWakeup handler to run from within the context of the
  // mixing execution domain.
  void Wakeup();

  // Accessors for the current plug state of the device.
  //
  // In addition to publishing and unpublishing streams when codecs are
  // attached/removed to/from hot pluggable buses (such as USB), some codecs
  // have the ability to detect the plugged or unplugged state of external
  // connectors (such as a 3.5mm audio jack).  Drivers can report this
  // plugged/unplugged state as well as the time of the last state change.
  // Currently this information is used in the Audio Server to implement simple
  // routing policies for AudioRenderers and AudioCapturers.
  //
  // plugged   : true when an audio output stream is either hardwired, or
  //             believes that it has something connected to its plug.
  // plug_time : The last time (according to zx_clock_get(ZX_CLOCK_MONOTONIC) at
  //             which the plugged/unplugged state of the output stream last
  //             changed.
  bool plugged() const { return plugged_; }
  zx_time_t plug_time() const { return plug_time_; }
  const std::unique_ptr<AudioDriver>& driver() const { return driver_; }
  uint64_t token() const;
  uint64_t GetKey() const { return token(); }
  bool activated() const { return activated_; }

  // NotifyDestFormatPreference
  //
  // Called by clients who are destinations of ours to inform us of their
  // preferred format.
  //
  // TODO(johngro) : Remove this once device driver format selection is under
  // control of the policy manager layer instead of here.
  virtual void NotifyDestFormatPreference(
      const fuchsia::media::AudioMediaTypeDetailsPtr& fmt)
      FXL_LOCKS_EXCLUDED(mix_domain_->token()) {}

  // GetSourceFormatPreference
  //
  // Returns the format that this AudioDevice prefers to use when acting as a
  // source of audio (either an input, or an output being looped back)
  //
  // TODO(johngro) : Remove this once we have policy in place.  Users should be
  // talking to the policy manager to know what inputs and outputs exist, and
  // what formats they support, and to influence what their capturers can be
  // bound to or not.  "Preference" of an audio device is not a concept which
  // belongs in the mixer.
  virtual fuchsia::media::AudioMediaTypeDetailsPtr GetSourceFormatPreference() {
    return nullptr;
  }

 protected:
  friend class fbl::RefPtr<AudioDevice>;
  ~AudioDevice() override;
  AudioDevice(Type type, AudioDeviceManager* manager);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Methods which may be implemented by derived classes to customize behavior.
  //
  //////////////////////////////////////////////////////////////////////////////

  // Init
  //
  // Called during startup on the AudioServer's main message loop thread.  No
  // locks are being held at this point.  Derived classes should begin the
  // process of driver initialization at this point.  Return ZX_OK if things
  // have started and we are waitin gfor driver init.
  virtual zx_status_t Init();

  // Cleanup
  //
  // Called at shutdown on the AudioServer's main message loop thread to allow
  // derived classes to clean up any allocated resources.  All pending
  // processing callbacks have either been nerfed or run till completion.  All
  // audio other objects have been disconnected/unlinked.  No locks are being
  // held.
  virtual void Cleanup();

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
  virtual void OnWakeup()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) = 0;

  // ActivateSelf
  //
  // Send a message to the audio device manager to let it know that we are ready
  // to be added to the set of active devices.
  void ActivateSelf() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // ShutdownSelf
  //
  // Kick off the process of shooting ourselves in the head.  Note, after this
  // method has been called, no new callbacks may be scheduled.  As soon as the
  // main message loop finds out about our shutdown request, it will complete
  // the process of shutting us down, unlinking us from our renderers and
  // calling the Cleanup method.
  void ShutdownSelf() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Check the shutting down flag.  We are in the process of shutting down when
  // we have become deactivated at the dispatcher framework level.
  inline bool is_shutting_down() const {
    return (!mix_domain_ || mix_domain_->deactivated());
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // AudioDriver hooks.
  //
  // Hooks used by encapsulated AudioDriver instances to notify AudioDevices
  // about internal state machine changes.
  virtual void OnDriverInfoFetched()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()){};

  virtual void OnDriverConfigComplete()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()){};

  virtual void OnDriverStartComplete()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()){};

  virtual void OnDriverStopComplete()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()){};

  virtual void OnDriverPlugStateChange(bool plugged, zx_time_t plug_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()){};

  //////////////////////////////////////////////////////////////////////////////
  //
  // Other methods.
  //

  // UpdatePlugState
  //
  // Called by the audio output manager on the main message loop when it has
  // been notified of a plug state change for the output.  Used to update the
  // internal bookkeeping about the current plugged/unplugged state.  This
  // method may also be used by derived classes during Init to set an initial
  // plug state.
  //
  // Returns true if the plug state has changed, or false otherwise.
  bool UpdatePlugState(bool plugged, zx_time_t plug_time);

  // AudioDriver accessors.
  const fbl::RefPtr<DriverRingBuffer>& driver_ring_buffer() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  const TimelineFunction& driver_clock_mono_to_ring_pos_bytes() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  AudioDeviceManager* manager_;

  // State used to manage asynchronous processing using the dispatcher
  // framework.
  fbl::RefPtr<::dispatcher::ExecutionDomain> mix_domain_;
  fbl::RefPtr<::dispatcher::WakeupEvent> mix_wakeup_;

  // Driver object which will manage most interactions with the low level driver
  // for us.
  std::unique_ptr<AudioDriver> driver_;

 private:
  // It's always nice when you manager is also your friend.  Seriously though,
  // the AudioDeviceManager gets to call Startup and Shutdown, no one else
  // (including derived classes) should be able to.
  friend class AudioDeviceManager;
  friend class AudioDriver;
  friend struct PendingInitListTraits;

  // DeactivateDomain
  //
  // deactivate our execution domain (if it exists) and synchronize with any
  // operations taking place in the domain.
  void DeactivateDomain() FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Called from the AudioDeviceManager after an output has been created.
  // Gives derived classes a chance to set up hardware, then sets up the
  // machinery needed for scheduling processing tasks and schedules the first
  // processing callback immediately in order to get the process running.
  zx_status_t Startup();

  // Called from the AudioDeviceManager on the main message loop
  // thread.  Makes certain that the process of shutdown has started,
  // synchronizes with any processing tasks which were executing at the time,
  // then finishes the shutdown process by unlinking from all renderers and
  // cleaning up all resources.
  void Shutdown();

  // Called from the AudioDeviceManager when it moves an audio device from its
  // "pending init" set over to its "active" set .
  void SetActivated() {
    FXL_DCHECK(!activated());
    activated_ = true;
  }

  // Plug state is protected by the fact that it is only ever accessed on the
  // main message loop thread.
  bool plugged_ = false;
  zx_time_t plug_time_ = 0;

  volatile bool shut_down_ = false;
  volatile bool activated_ = false;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_H_
