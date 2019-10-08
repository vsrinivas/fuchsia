// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_MANAGER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_MANAGER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_device_settings_persistence.h"
#include "src/media/audio/audio_core/audio_input.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/audio_plug_detector_impl.h"
#include "src/media/audio/audio_core/audio_renderer_impl.h"
#include "src/media/audio/audio_core/object_registry.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/lib/effects_loader/effects_loader.h"

namespace media::audio {

class AudioCapturerImpl;
class SystemGainMuteProvider;

class AudioDeviceManager : public fuchsia::media::AudioDeviceEnumerator, public ObjectRegistry {
 public:
  AudioDeviceManager(ThreadingModel* threading_model, EffectsLoader* effects_loader,
                     AudioDeviceSettingsPersistence* device_settings_persistence,
                     const SystemGainMuteProvider& system_gain_mute);
  ~AudioDeviceManager();

  ThreadingModel& threading_model() { return threading_model_; }

  // Initialize the input/output manager.
  zx_status_t Init();

  void EnableDeviceSettings(bool enabled) {
    device_settings_persistence_.EnableDeviceSettings(enabled);
  }

  // Blocking call. Called by the service, once, when it is time to shutdown the service
  // implementation. While this function is blocking, it must never block for long. Our process is
  // going away; this is our last chance to perform a clean shutdown. If an unclean shutdown must
  // be performed in order to implode in a timely fashion, so be it.
  //
  // Shutdown must be idempotent and safe to call from this object's destructor (although this
  // should never be necessary). If a shutdown called from this destructor must do real work,
  // something has gone Very Seriously Wrong.
  void Shutdown();

  // Add a new device-enumerator client. Called from service framework when a new client connects.
  void AddDeviceEnumeratorClient(
      fidl::InterfaceRequest<fuchsia::media::AudioDeviceEnumerator> request);

  // Select the initial set of outputs for a newly-configured AudioRenderer.
  void SelectOutputsForAudioRenderer(AudioRendererImpl* audio_renderer);

  // Link an output to an AudioRenderer.
  void LinkOutputToAudioRenderer(AudioOutput* output, AudioRendererImpl* audio_renderer);

  void SetRoutingPolicy(fuchsia::media::AudioOutputRoutingPolicy policy);

  // SetSystemGain/Mute has been called. 'changed' tells us whether System Gain or Mute values
  // actually changed. If not, only update devices that (because of calls to SetDeviceGain) have
  // diverged from System settings.
  void OnSystemGain(bool changed);

  // |media::audio::ObjectRegistry|
  void AddAudioRenderer(fbl::RefPtr<AudioRendererImpl> audio_renderer) override;
  void RemoveAudioRenderer(AudioRendererImpl* audio_renderer) override;
  void AddAudioCapturer(const fbl::RefPtr<AudioCapturerImpl>& audio_capturer) override;
  void RemoveAudioCapturer(AudioCapturerImpl* audio_capturer) override;
  void AddDevice(const fbl::RefPtr<AudioDevice>& device) override;
  void ActivateDevice(const fbl::RefPtr<AudioDevice>& device) override;
  void RemoveDevice(const fbl::RefPtr<AudioDevice>& device) override;
  void OnPlugStateChanged(const fbl::RefPtr<AudioDevice>& device, bool plugged,
                          zx::time plug_time) override;

  // |fuchsia::media::AudioDeviceEnumerator|
  void GetDevices(GetDevicesCallback cbk) final;
  void GetDeviceGain(uint64_t device_token, GetDeviceGainCallback cbk) final;
  void SetDeviceGain(uint64_t device_token, fuchsia::media::AudioGainInfo gain_info,
                     uint32_t set_flags) final;
  void GetDefaultInputDevice(GetDefaultInputDeviceCallback cbk) final;
  void GetDefaultOutputDevice(GetDefaultOutputDeviceCallback cbk) final;
  void AddDeviceByChannel(::zx::channel device_channel, std::string device_name,
                          bool is_input) final;

 private:
  void ActivateDeviceWithSettings(fbl::RefPtr<AudioDevice> device,
                                  fbl::RefPtr<AudioDeviceSettings> settings);
  // Find the most-recently plugged device (per type: input or output) excluding throttle_output. If
  // allow_unplugged, return the most-recently UNplugged device if no plugged devices are found --
  // otherwise return nullptr.
  fbl::RefPtr<AudioDevice> FindLastPlugged(AudioObject::Type type, bool allow_unplugged = false);

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

  // Methods to handle routing policy -- when an existing device is unplugged or completely removed,
  // or when a new device is plugged or added to the system.
  void OnDeviceUnplugged(const fbl::RefPtr<AudioDevice>& device, zx::time plug_time);
  void OnDevicePlugged(const fbl::RefPtr<AudioDevice>& device, zx::time plug_time);

  void LinkToAudioCapturers(const fbl::RefPtr<AudioDevice>& device);

  // Send notification to users that this device's gain settings have changed.
  void NotifyDeviceGainChanged(const AudioDevice& device);

  // Re-evaluate which device is the default. Notify users, if this has changed.
  void UpdateDefaultDevice(bool input);

  // Update a device gain to the "system" gain exposed by the top-level service.
  //
  // TODO(johngro): Remove this when we remove system gain entirely.
  void UpdateDeviceToSystemGain(const fbl::RefPtr<AudioDevice>& device);

  ThreadingModel& threading_model_;

  // Pointer to System gain/mute values. This pointer cannot be bad while we still exist.
  const SystemGainMuteProvider& system_gain_mute_;
  // Load and create audio effects.
  [[maybe_unused]] EffectsLoader& effects_loader_;

  // The set of AudioDeviceEnumerator clients we are currently tending to.
  fidl::BindingSet<fuchsia::media::AudioDeviceEnumerator> bindings_;

  // Our sets of currently active audio devices, AudioCapturers, and AudioRenderers.
  //
  // These must only be manipulated on main message loop thread. No synchronization should be needed
  fbl::WAVLTree<uint64_t, fbl::RefPtr<AudioDevice>> devices_pending_init_;
  fbl::WAVLTree<uint64_t, fbl::RefPtr<AudioDevice>> devices_;
  fbl::DoublyLinkedList<fbl::RefPtr<AudioCapturerImpl>> audio_capturers_;
  fbl::DoublyLinkedList<fbl::RefPtr<AudioRendererImpl>> audio_renderers_;

  // The special throttle output always exists and is used by every renderer.
  fbl::RefPtr<AudioOutput> throttle_output_;

  // A helper class we will use to detect plug/unplug events for audio devices
  AudioPlugDetectorImpl plug_detector_;

  AudioDeviceSettingsPersistence& device_settings_persistence_;

  // State which affects routing policy.
  fuchsia::media::AudioOutputRoutingPolicy routing_policy_ =
      fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT;
  uint64_t default_output_token_ = ZX_KOID_INVALID;
  uint64_t default_input_token_ = ZX_KOID_INVALID;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_MANAGER_H_
