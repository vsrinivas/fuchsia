// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_MANAGER_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_MANAGER_H_

#include <set>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/bin/media/audio_server/audio_device.h"
#include "garnet/bin/media/audio_server/audio_input.h"
#include "garnet/bin/media/audio_server/audio_output.h"
#include "garnet/bin/media/audio_server/audio_plug_detector.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "lib/fidl/cpp/binding_set.h"

namespace media {
namespace audio {

class AudioCapturerImpl;

class AudioDeviceManager : public ::fuchsia::media::AudioDeviceEnumerator {
 public:
  explicit AudioDeviceManager(AudioServerImpl* server);
  ~AudioDeviceManager();

  // Initialize the output manager.  Called from the service implementation,
  // once, at startup time.  Should...
  //
  // 1) Initialize the mixing thread pool.
  // 2) Instantiate all of the built-in audio output devices.
  // 3) Being monitoring for plug/unplug events for pluggable audio output
  //    devices.
  zx_status_t Init();

  // Blocking call.  Called by the service, once, when it is time to shutdown
  // the service implementation.  While this function is blocking, it must never
  // block for long.  Our process is going away; this is our last chance to
  // perform a clean shutdown.  If an unclean shutdown must be performed in
  // order to implode in a timely fashion, so be it.
  //
  // Shutdown must be idempotent, and safe to call from the output manager's
  // destructor, although it should never be necessary to do so.  If the
  // shutdown called from the destructor has to do real work, something has gone
  // Very Seriously Wrong.
  void Shutdown();

  // Add a new client for the device enumerator functionality.  Called from the
  // service framework each time a new client attempts to connect.
  void AddDeviceEnumeratorClient(zx::channel ch);

  // Add a renderer to the set of active audio renderers.
  void AddRenderer(fbl::RefPtr<AudioRendererImpl> renderer) {
    FXL_DCHECK(renderer);
    renderers_.push_back(std::move(renderer));
  }

  // Remove a renderer from the set of active audio renderers.
  void RemoveRenderer(AudioRendererImpl* renderer) {
    FXL_DCHECK(renderer != nullptr);
    FXL_DCHECK(renderer->InContainer());
    renderers_.erase(*renderer);
  }

  // Select the initial set of outputs for a renderer which has just been
  // configured.
  void SelectOutputsForRenderer(AudioRendererImpl* renderer);

  // Link an output to an audio renderer
  void LinkOutputToRenderer(AudioOutput* output, AudioRendererImpl* renderer);

  // Add/remove a capturer to/from the set of active audio capturers.
  void AddCapturer(fbl::RefPtr<AudioCapturerImpl> capturer);
  void RemoveCapturer(AudioCapturerImpl* capturer);

  // Schedule a closure to run on our encapsulating server's main message loop.
  void ScheduleMainThreadTask(fit::closure task);

  // Begin the process of initializing a device and add it to the set of device
  // which are waiting to be initialized.
  //
  // Called from the plug detector when a new stream device shows up for the
  // first time.
  zx_status_t AddDevice(const fbl::RefPtr<AudioDevice>& device);

  // Move a device from the pending init list to the active device_'s list.
  // Notify users of the presence of a new device, and re-evaluate policy
  // decisions.
  void ActivateDevice(const fbl::RefPtr<AudioDevice>& device);

  // Shutdown the specified audio device and remove it from the appropriate set
  // of active devices.
  void RemoveDevice(const fbl::RefPtr<AudioDevice>& device);

  // Handles a plugged/unplugged state change for the supplied audio device.
  void HandlePlugStateChange(const fbl::RefPtr<AudioDevice>& device,
                             bool plugged, zx_time_t plug_time);

  void SetRoutingPolicy(fuchsia::media::AudioOutputRoutingPolicy policy);

  static inline bool ValidateRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy policy) {
    switch (policy) {
      case fuchsia::media::AudioOutputRoutingPolicy::kLastPluggedOutput:
      case fuchsia::media::AudioOutputRoutingPolicy::kAllPluggedOutputs:
        return true;
        // Note: no default: handler here.  If someone adds a new policy to the
        // enum but forgets to update this code, we want a Build Break, to
        // notify us that we need to handle the new policy.
    }

    return false;
  }

  // Master gain control.  Only safe to access via the main message loop thread.
  void SetMasterGain(float db_gain);
  float master_gain() const { return master_gain_; }

  // Implementation of the AudioDeviceEnumerator FIDL interface.
  void GetDevices(GetDevicesCallback cbk) final;
  void GetDeviceGain(uint64_t device_token, GetDeviceGainCallback cbk) final;
  void SetDeviceGain(uint64_t device_token,
                     ::fuchsia::media::AudioGainInfo gain_info,
                     uint32_t set_flags) final;
  void GetDefaultInputDevice(GetDefaultInputDeviceCallback cbk) final;
  void GetDefaultOutputDevice(GetDefaultOutputDeviceCallback cbk) final;

 private:
  // Find the last plugged input or output (excluding the throttle_output) in
  // the system.  If allow_unplugged is true, the most recently unplugged
  // input/output will be returned if no plugged outputs can be found.
  // Otherwise, nullptr.
  fbl::RefPtr<AudioDevice> FindLastPlugged(AudioObject::Type type,
                                           bool allow_unplugged = false);

  fbl::RefPtr<AudioOutput> FindLastPluggedOutput(bool allow_unplugged = false) {
    auto dev = FindLastPlugged(AudioObject::Type::Output, allow_unplugged);
    FXL_DCHECK(!dev || (dev->type() == AudioObject::Type::Output));
    return fbl::RefPtr<AudioOutput>::Downcast(std::move(dev));
  }

  fbl::RefPtr<AudioInput> FindLastPluggedInput(bool allow_unplugged = false) {
    auto dev = FindLastPlugged(AudioObject::Type::Input, allow_unplugged);
    FXL_DCHECK(!dev || (dev->type() == AudioObject::Type::Input));
    return fbl::RefPtr<AudioInput>::Downcast(std::move(dev));
  }

  // Methods for dealing with routing policy when a device becomes unplugged or
  // completely removed from the system, or has become plugged/newly added to
  // the system.
  void OnDeviceUnplugged(const fbl::RefPtr<AudioDevice>& device,
                         zx_time_t plug_time);
  void OnDevicePlugged(const fbl::RefPtr<AudioDevice>& device,
                       zx_time_t plug_time);

  void LinkToCapturers(const fbl::RefPtr<AudioDevice>& device);

  // Re-evaluate which device should be the default device, and send
  // notifications to users if the default device has changed.
  void UpdateDefaultDevice(bool input);

  // A pointer to the server which encapsulates us.  It is not possible for
  // this pointer to be bad while we still exist.
  AudioServerImpl* server_;

  // The set of AudioDeviceEnumerator clients we are currently tending to.
  fidl::BindingSet<::fuchsia::media::AudioDeviceEnumerator> bindings_;

  // Our sets of currently active audio devices, capturers, and renderers.
  //
  // Contents of these collections must only be manipulated on the main message
  // loop thread, so no synchronization should be needed.
  fbl::WAVLTree<uint64_t, fbl::RefPtr<AudioDevice>> devices_pending_init_;
  fbl::WAVLTree<uint64_t, fbl::RefPtr<AudioDevice>> devices_;
  fbl::DoublyLinkedList<fbl::RefPtr<AudioCapturerImpl>> capturers_;
  fbl::DoublyLinkedList<fbl::RefPtr<AudioRendererImpl>> renderers_;

  // The special throttle output.  This output always exists, and is always used
  // by all renderers.
  fbl::RefPtr<AudioOutput> throttle_output_;

  // A helper class we will use to detect plug/unplug events for audio devices
  AudioPlugDetector plug_detector_;

  // Current master gain setting (in dB).
  //
  // TODO(johngro): remove this when we have a policy manager which controls
  // gain on a per-output basis.
  float master_gain_ = -20.0;

  // State which affects routing policy.
  fuchsia::media::AudioOutputRoutingPolicy routing_policy_ =
      fuchsia::media::AudioOutputRoutingPolicy::kLastPluggedOutput;
  uint64_t default_output_token_ = ZX_KOID_INVALID;
  uint64_t default_input_token_ = ZX_KOID_INVALID;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_AUDIO_DEVICE_MANAGER_H_
