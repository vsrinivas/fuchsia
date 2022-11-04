// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DEVICE_MANAGER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DEVICE_MANAGER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <unordered_map>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/v1/audio_device.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/audio_input.h"
#include "src/media/audio/audio_core/v1/audio_output.h"
#include "src/media/audio/audio_core/v1/base_renderer.h"
#include "src/media/audio/audio_core/v1/device_registry.h"
#include "src/media/audio/audio_core/v1/plug_detector.h"
#include "src/media/audio/audio_core/v1/threading_model.h"

namespace media::audio {

class BaseCapturer;
class EffectsLoaderV2;

class AudioDeviceManager : public fuchsia::media::AudioDeviceEnumerator, public DeviceRegistry {
 public:
  AudioDeviceManager(ThreadingModel& threading_model, std::unique_ptr<PlugDetector> plug_detector,
                     LinkMatrix& link_matrix, ProcessConfig& process_config,
                     std::shared_ptr<AudioCoreClockFactory> clock_factory,
                     DeviceRouter& device_router, EffectsLoaderV2* effects_loader_v2);
  ~AudioDeviceManager();

  fidl::InterfaceRequestHandler<fuchsia::media::AudioDeviceEnumerator> GetFidlRequestHandler() {
    return bindings_.GetHandler(this);
  }

  ThreadingModel& threading_model() { return threading_model_; }

  // Initialize the input/output manager.
  zx_status_t Init();

  // Blocking call. Called by the service, once, when it is time to shutdown the service
  // implementation. While this function is blocking, it must never block for long. Our process is
  // going away; this is our last chance to perform a clean shutdown. If an unclean shutdown must
  // be performed in order to implode in a timely fashion, so be it.
  //
  // Shutdown must be idempotent and safe to call from this object's destructor (although this
  // should never be necessary). If a shutdown called from this destructor must do real work,
  // something has gone Very Seriously Wrong.
  void Shutdown();

  // Sends an update message to each effect with the name 'instance_name' across all devices.
  // If 'persist' is true, the effect update is persisted and applied to new devices as they are
  // plugged. Only the latest update will be persisted for each 'instance_name'.
  //
  // Returns UpdateEffectError::INVALID_CONFIG if any effect matching 'instance_name' is found, but
  // rejects 'message'. Returns UpdateEffectError::NOT_FOUND if no effect is found across any
  // device. Returns success if at least one effect named 'instance_name' has accepted 'message'
  // without any other effects matching 'effect_name' rejecting the 'message'.
  fpromise::promise<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& message, bool persist = false);

  // Sends an update message to the effect specified by 'instance_name' for the device specified by
  // 'device_id'.
  //
  // Return values match those of 'UpdateEffect' above, with the addition of an
  // UpdateEffectError::NOT_FOUND if the device specified by 'device_id' is not found.
  fpromise::promise<void, fuchsia::media::audio::UpdateEffectError> UpdateDeviceEffect(
      const std::string device_id, const std::string& instance_name, const std::string& message);

  fpromise::promise<void, zx_status_t> UpdatePipelineConfig(const std::string device_id,
                                                            const PipelineConfig& config,
                                                            const VolumeCurve& volume_curve);

  // |media::audio::DeviceRegistry|
  void AddDevice(const std::shared_ptr<AudioDevice>& device) override;
  void ActivateDevice(const std::shared_ptr<AudioDevice>& device) override;
  void RemoveDevice(const std::shared_ptr<AudioDevice>& device) override;
  void OnPlugStateChanged(const std::shared_ptr<AudioDevice>& device, bool plugged,
                          zx::time plug_time) override;
  std::vector<fuchsia::media::AudioDeviceInfo> GetDeviceInfos() override;

  // |fuchsia::media::AudioDeviceEnumerator|
  void GetDevices(GetDevicesCallback cbk) final;
  void GetDeviceGain(uint64_t device_token, GetDeviceGainCallback cbk) final;
  void SetDeviceGain(uint64_t device_token, fuchsia::media::AudioGainInfo gain_info,
                     fuchsia::media::AudioGainValidFlags) final;
  void GetDefaultInputDevice(GetDefaultInputDeviceCallback cbk) final;
  void GetDefaultOutputDevice(GetDefaultOutputDeviceCallback cbk) final;
  void AddDeviceByChannel(
      std::string device_name, bool is_input,
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> channel) final;

 private:
  // Find the most-recently plugged device (per type: input or output) excluding throttle_output. If
  // allow_unplugged, return the most-recently UNplugged device if no plugged devices are found --
  // otherwise return nullptr.
  std::shared_ptr<AudioDevice> FindLastPlugged(AudioObject::Type type,
                                               bool allow_unplugged = false);

  std::shared_ptr<AudioOutput> FindLastPluggedOutput(bool allow_unplugged = false) {
    auto dev = FindLastPlugged(AudioObject::Type::Output, allow_unplugged);
    FX_DCHECK(!dev || (dev->type() == AudioObject::Type::Output));
    return std::static_pointer_cast<AudioOutput>(std::move(dev));
  }

  std::shared_ptr<AudioInput> FindLastPluggedInput(bool allow_unplugged = false) {
    auto dev = FindLastPlugged(AudioObject::Type::Input, allow_unplugged);
    FX_DCHECK(!dev || (dev->type() == AudioObject::Type::Input));
    return std::static_pointer_cast<AudioInput>(std::move(dev));
  }

  // Methods to handle routing policy -- when an existing device is unplugged or completely removed,
  // or when a new device is plugged or added to the system.
  void OnDeviceUnplugged(const std::shared_ptr<AudioDevice>& device, zx::time plug_time);
  void OnDevicePlugged(const std::shared_ptr<AudioDevice>& device, zx::time plug_time);

  // Send notification to users that this device's gain settings have changed.
  void NotifyDeviceGainChanged(const AudioDevice& device);

  // Re-evaluate which device is the default. Notify users, if this has changed.
  void UpdateDefaultDevice(bool input);

  ThreadingModel& threading_model_;
  std::unique_ptr<PlugDetector> plug_detector_;
  LinkMatrix& link_matrix_;
  ProcessConfig& process_config_;
  std::shared_ptr<AudioCoreClockFactory> clock_factory_;
  DeviceRouter& device_router_;
  EffectsLoaderV2* effects_loader_v2_;

  // The set of AudioDeviceEnumerator clients we are currently tending to.
  fidl::BindingSet<fuchsia::media::AudioDeviceEnumerator> bindings_;

  // Our sets of currently active audio devices, AudioCapturers, and AudioRenderers.
  //
  // These must only be manipulated on main message loop thread. No synchronization should be
  // needed.

  // These maps are keyed on device token.
  std::unordered_map<uint64_t, std::shared_ptr<AudioDevice>> devices_pending_init_;
  std::unordered_map<uint64_t, std::shared_ptr<AudioDevice>> devices_;

  uint64_t default_output_token_ = ZX_KOID_INVALID;
  uint64_t default_input_token_ = ZX_KOID_INVALID;

  // Persisted effects updates. Mapping from instant_name to message.
  std::unordered_map<std::string, std::string> persisted_effects_updates_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DEVICE_MANAGER_H_
