// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_device_manager.h"

#include <lib/fpromise/promise.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <string>

#include "src/media/audio/audio_core/shared/reporter.h"
#include "src/media/audio/audio_core/v1/audio_core_impl.h"
#include "src/media/audio/audio_core/v1/base_capturer.h"
#include "src/media/audio/audio_core/v1/base_renderer.h"
#include "src/media/audio/audio_core/v1/driver_output.h"
#include "src/media/audio/audio_core/v1/plug_detector.h"

namespace media::audio {

AudioDeviceManager::AudioDeviceManager(ThreadingModel& threading_model,
                                       std::unique_ptr<PlugDetector> plug_detector,
                                       LinkMatrix& link_matrix, ProcessConfig& process_config,
                                       std::shared_ptr<AudioCoreClockFactory> clock_factory,
                                       DeviceRouter& device_router,
                                       EffectsLoaderV2* effects_loader_v2)
    : threading_model_(threading_model),
      plug_detector_(std::move(plug_detector)),
      link_matrix_(link_matrix),
      process_config_(process_config),
      clock_factory_(clock_factory),
      device_router_(device_router),
      effects_loader_v2_(effects_loader_v2) {}

AudioDeviceManager::~AudioDeviceManager() {
  Shutdown();
  FX_DCHECK(devices_.empty());
}

// Configure this admin singleton object to manage audio device instances.
zx_status_t AudioDeviceManager::Init() {
  TRACE_DURATION("audio", "AudioDeviceManager::Init");

  // Start monitoring for plug/unplug events of pluggable audio output devices.
  zx_status_t res =
      plug_detector_->Start(fit::bind_member<&AudioDeviceManager::AddDeviceByChannel>(this));
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "AudioDeviceManager failed to start plug detector";
    return res;
  }

  return ZX_OK;
}

// We are no longer managing audio devices, unwind everything.
void AudioDeviceManager::Shutdown() {
  TRACE_DURATION("audio", "AudioDeviceManager::Shutdown");
  plug_detector_->Stop();

  std::vector<fpromise::promise<void>> device_promises;
  for (auto& [_, device] : devices_pending_init_) {
    device_promises.push_back(device->Shutdown());
  }
  devices_pending_init_.clear();

  for (auto& [_, device] : devices_) {
    device_promises.push_back(device->Shutdown());
  }
  devices_.clear();

  fpromise::run_single_threaded(fpromise::join_promise_vector(std::move(device_promises)));
}

fpromise::promise<void, fuchsia::media::audio::UpdateEffectError> AudioDeviceManager::UpdateEffect(
    const std::string& instance_name, const std::string& config, bool persist) {
  if (persist) {
    persisted_effects_updates_[instance_name] = config;
  }
  std::vector<fpromise::promise<void, fuchsia::media::audio::UpdateEffectError>> promises;
  for (auto& [_, device] : devices_) {
    promises.push_back(device->UpdateEffect(instance_name, config));
  }
  return fpromise::join_promise_vector(std::move(promises))
      .then([](fpromise::result<
                std::vector<fpromise::result<void, fuchsia::media::audio::UpdateEffectError>>>&
                   results) -> fpromise::result<void, fuchsia::media::audio::UpdateEffectError> {
        FX_DCHECK(results.is_ok()) << "fpromise::join_promise_vector returns an error";
        bool found = false;
        for (const auto& result : results.value()) {
          if (result.is_error() &&
              result.error() == fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG) {
            return result;
          }
          if (result.is_ok()) {
            found = true;
          }
        }
        if (found) {
          return fpromise::ok();
        } else {
          return fpromise::error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
        }
      });
}

fpromise::promise<void, fuchsia::media::audio::UpdateEffectError>
AudioDeviceManager::UpdateDeviceEffect(const std::string device_id,
                                       const std::string& instance_name,
                                       const std::string& message) {
  auto devices = GetDeviceInfos();
  const auto dev = std::find_if(devices.begin(), devices.end(), [&device_id](auto candidate) {
    return candidate.unique_id == device_id;
  });
  if (dev == devices.end()) {
    return fpromise::make_error_promise(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
  }
  auto device = devices_[dev->token_id];
  FX_DCHECK(device);

  return device->UpdateEffect(instance_name, message)
      .then([](fpromise::result<void, fuchsia::media::audio::UpdateEffectError>& result)
                -> fpromise::result<void, fuchsia::media::audio::UpdateEffectError> {
        if (result.is_ok()) {
          return fpromise::ok();
        }
        if (result.error() == fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG) {
          return result;
        } else {
          return fpromise::error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
        }
      });
}

fpromise::promise<void, zx_status_t> AudioDeviceManager::UpdatePipelineConfig(
    const std::string device_id, const PipelineConfig& pipeline_config,
    const VolumeCurve& volume_curve) {
  auto devices = GetDeviceInfos();
  const auto dev = std::find_if(devices.begin(), devices.end(), [&device_id](auto candidate) {
    return candidate.unique_id == device_id;
  });
  if (dev == devices.end()) {
    return fpromise::make_error_promise(ZX_ERR_NOT_FOUND);
  }
  auto device = devices_[dev->token_id];
  FX_DCHECK(device);

  // UpdatePipelineConfig is only valid on a device that is currently routable; the routable state
  // protects from devices being plugged or unplugged during update of the PipelineConfig,
  // as well as ensures only one update to the PipelineConfig will be processed at a time.
  if (!device->routable()) {
    FX_LOGS(INFO) << "Device unroutable BAD_STATE (token_id " << dev->token_id << ", unique_id '"
                  << device_id << "')";
    return fpromise::make_error_promise(ZX_ERR_BAD_STATE);
  }

  // UpdatePipelineConfig is only valid on a device without links (for the purpose of effects
  // tuning). As such, the device is removed from route_graph to ensure all links are removed.
  if (device->plugged()) {
    device_router_.RemoveDeviceFromRoutes(device.get());
  }
  FX_DCHECK(link_matrix_.DestLinkCount(*device) == 0);
  FX_DCHECK(link_matrix_.SourceLinkCount(*device) == 0);

  device->UpdateRoutableState(false);
  auto profile_params = DeviceConfig::OutputDeviceProfile::Parameters{
      .pipeline_config = pipeline_config, .volume_curve = volume_curve};
  return device->UpdateDeviceProfile(profile_params).and_then([this, device]() {
    device->UpdateRoutableState(true);
    if (device->plugged()) {
      device_router_.AddDeviceToRoutes(device.get());
    }
  });
}

void AudioDeviceManager::AddDevice(const std::shared_ptr<AudioDevice>& device) {
  TRACE_DURATION("audio", "AudioDeviceManager::AddDevice");
  FX_DCHECK(device != nullptr);

  threading_model_.FidlDomain().executor()->schedule_task(
      device->Startup()
          .and_then([this, device]() mutable {
            devices_pending_init_.insert({device->token(), std::move(device)});
          })
          .or_else([device](zx_status_t& error) {
            FX_PLOGS(ERROR, error) << "AddDevice failed";
            Reporter::Singleton().FailedToStartDevice(device->name());
            device->Shutdown();
          }));
}

void AudioDeviceManager::ActivateDevice(const std::shared_ptr<AudioDevice>& device) {
  TRACE_DURATION("audio", "AudioDeviceManager::ActivateDevice");
  FX_DCHECK(device != nullptr);

  // Have we already been removed from the pending list?  If so, the device is
  // already shutting down and there is nothing to be done.
  if (devices_pending_init_.find(device->token()) == devices_pending_init_.end()) {
    return;
  }

  // If this device is still waiting for initialization, move it over to the set of active devices.
  // Otherwise (if not waiting for initialization), we've been removed.
  auto dev = devices_pending_init_.extract(device->token());
  if (!dev) {
    return;
  }

  // Set software gain.
  auto driver = device->driver();
  const float software_gain_db = device->is_output()
                                     ? process_config_.device_config()
                                           .output_device_profile(driver->persistent_unique_id())
                                           .software_gain_db()
                                     : process_config_.device_config()
                                           .input_device_profile(driver->persistent_unique_id())
                                           .software_gain_db();
  device->SetSoftwareGainInfo({
      .gain_db = software_gain_db,
      .flags = static_cast<fuchsia::media::AudioGainInfoFlags>(0),
  });

  devices_.insert(std::move(dev));
  device->SetActivated();

  // Apply persisted effects updates.
  std::vector<fpromise::promise<void, void>> promises;
  for (auto it : persisted_effects_updates_) {
    std::string instance_name = it.first;
    std::string config = it.second;
    promises.push_back(
        device->UpdateEffect(instance_name, config)
            .then([instance_name, config](
                      fpromise::result<void, fuchsia::media::audio::UpdateEffectError>& result) {
              if (result.is_error()) {
                FX_LOGS_FIRST_N(ERROR, 10) << "Unable to update effect " << instance_name
                                           << ", error code " << static_cast<int>(result.error());
              }
            }));
  }
  threading_model_.FidlDomain().executor()->schedule_task(
      fpromise::join_promise_vector(std::move(promises)));

  // Notify interested users of the new device.
  auto info = device->GetDeviceInfo();

  // We always report is_default as false in the OnDeviceAdded event. There will be a following
  // DefaultDeviceChange event that will signal if this device is now the default.
  info.is_default = false;

  for (auto& client : bindings_.bindings()) {
    client->events().OnDeviceAdded(info);
  }

  if (device->plugged()) {
    OnDevicePlugged(device, device->plug_time());
  }
}

void AudioDeviceManager::RemoveDevice(const std::shared_ptr<AudioDevice>& device) {
  TRACE_DURATION("audio", "AudioDeviceManager::RemoveDevice");
  FX_DCHECK(device != nullptr);

  FX_LOGS(INFO) << "Removing " << (device->is_input() ? "input" : "output") << " '"
                << device->name() << "'";

  // If device was active: reset the default (based on most-recently-plugged).
  OnPlugStateChanged(device, false, device->plug_time());
  device->Shutdown();

  auto& device_set = device->activated() ? devices_ : devices_pending_init_;
  device_set.erase(device->token());

  // If device was active: notify clients of the removal.
  if (device->activated()) {
    for (auto& client : bindings_.bindings()) {
      client->events().OnDeviceRemoved(device->token());
    }
  }
}

void AudioDeviceManager::OnPlugStateChanged(const std::shared_ptr<AudioDevice>& device,
                                            bool plugged, zx::time plug_time) {
  TRACE_DURATION("audio", "AudioDeviceManager::OnPlugStateChanged");
  FX_DCHECK(device != nullptr);

  // Update our bookkeeping for device's plug state. If no change, we're done.
  if (!device->UpdatePlugState(plugged, plug_time)) {
    // TODO(fxbug.dev/73947): remove after debugging
    FX_LOGS(INFO) << "Ignoring OnPlugStateChanged event (no change): "
                  << (device->is_input() ? "input" : "output") << " '" << device->name()
                  << "', plugged=" << plugged << ", t=" << plug_time.get();
    return;
  }

  // If the device is not yet activated, we should not be changing routes.
  bool activated = devices_.find(device->token()) != devices_.end();
  if (!activated) {
    // TODO(fxbug.dev/73947): remove after debugging
    FX_LOGS(INFO) << "Ignoring OnPlugStateChanged event (not activated): "
                  << (device->is_input() ? "input" : "output") << " '" << device->name()
                  << "', plugged=" << plugged << ", t=" << plug_time.get();
    return;
  }

  if (plugged) {
    OnDevicePlugged(device, plug_time);
  } else {
    OnDeviceUnplugged(device, plug_time);
  }
}

std::vector<fuchsia::media::AudioDeviceInfo> AudioDeviceManager::GetDeviceInfos() {
  TRACE_DURATION("audio", "AudioDeviceManager::GetDevices");
  std::vector<fuchsia::media::AudioDeviceInfo> ret;

  for (const auto& [_, dev] : devices_) {
    if (dev->token() != ZX_KOID_INVALID) {
      auto info = dev->GetDeviceInfo();
      info.is_default =
          (dev->token() == (dev->is_input() ? default_input_token_ : default_output_token_));
      ret.push_back(std::move(info));
    }
  }

  return ret;
}

void AudioDeviceManager::GetDevices(GetDevicesCallback cbk) { cbk(GetDeviceInfos()); }

void AudioDeviceManager::GetDeviceGain(uint64_t device_token, GetDeviceGainCallback cbk) {
  TRACE_DURATION("audio", "AudioDeviceManager::GetDeviceGain");

  auto it = devices_.find(device_token);
  if (it == devices_.end()) {
    cbk(ZX_KOID_INVALID, {});
    return;
  }

  auto [_, dev] = *it;
  FX_DCHECK(dev->device_settings() != nullptr);
  auto info = dev->device_settings()->GetGainInfo();
  cbk(device_token, info);
}

void AudioDeviceManager::SetDeviceGain(uint64_t device_token,
                                       fuchsia::media::AudioGainInfo gain_info,
                                       fuchsia::media::AudioGainValidFlags set_flags) {
  TRACE_DURATION("audio", "AudioDeviceManager::SetDeviceGain");
  auto it = devices_.find(device_token);
  if (it == devices_.end()) {
    return;
  }
  auto [_, dev] = *it;

  // SetGainInfo clamps out-of-range values (e.g. +infinity) into the device-
  // allowed gain range. NAN is undefined (signless); handle it here and exit.
  if (((set_flags & fuchsia::media::AudioGainValidFlags::GAIN_VALID) ==
       fuchsia::media::AudioGainValidFlags::GAIN_VALID) &&
      isnan(gain_info.gain_db)) {
    FX_LOGS(WARNING) << "Invalid device gain " << gain_info.gain_db << " dB -- making no change";
    return;
  }

  // Change the gain and then report the new settings to our clients.
  dev->SetGainInfo(gain_info, set_flags);
  NotifyDeviceGainChanged(*dev);
}

void AudioDeviceManager::GetDefaultInputDevice(GetDefaultInputDeviceCallback cbk) {
  cbk(default_input_token_);
}

void AudioDeviceManager::GetDefaultOutputDevice(GetDefaultOutputDeviceCallback cbk) {
  cbk(default_output_token_);
}

std::shared_ptr<AudioDevice> AudioDeviceManager::FindLastPlugged(AudioObject::Type type,
                                                                 bool allow_unplugged) {
  TRACE_DURATION("audio", "AudioDeviceManager::FindLastPlugged");
  FX_DCHECK((type == AudioObject::Type::Output) || (type == AudioObject::Type::Input));
  std::shared_ptr<AudioDevice> best = nullptr;

  // TODO(johngro): Consider tracking last-plugged times in a fbl::WAVLTree, so
  // this operation becomes O(1). N is pretty low right now, so the benefits do
  // not currently outweigh the complexity of maintaining this index.
  for (auto& [_, device] : devices_) {
    if (device->type() != type) {
      continue;
    }

    if ((best == nullptr) || (!best->plugged() && device->plugged()) ||
        ((best->plugged() == device->plugged()) && (best->plug_time() < device->plug_time()))) {
      best = device;
    }
  }

  FX_DCHECK((best == nullptr) || (best->type() == type));
  if (!allow_unplugged && best && !best->plugged()) {
    return nullptr;
  }

  return best;
}

void AudioDeviceManager::OnDeviceUnplugged(const std::shared_ptr<AudioDevice>& device,
                                           zx::time plug_time) {
  TRACE_DURATION("audio", "AudioDeviceManager::OnDeviceUnplugged");
  FX_DCHECK(device);
  FX_LOGS(INFO) << "Unplugged " << (device->is_input() ? "input" : "output") << " '"
                << device->name() << "' at t=" << plug_time.get();

  device->UpdatePlugState(/*plugged=*/false, plug_time);

  if (device->routable()) {
    device_router_.RemoveDeviceFromRoutes(device.get());
  }
  UpdateDefaultDevice(device->is_input());
}

void AudioDeviceManager::OnDevicePlugged(const std::shared_ptr<AudioDevice>& device,
                                         zx::time plug_time) {
  TRACE_DURATION("audio", "AudioDeviceManager::OnDevicePlugged");
  FX_DCHECK(device);
  FX_LOGS(INFO) << "Plugged " << (device->is_input() ? "input" : "output") << " '" << device->name()
                << "' at t=" << plug_time.get();

  device->UpdatePlugState(/*plugged=*/true, plug_time);

  if (device->routable()) {
    device_router_.AddDeviceToRoutes(device.get());
  }
  UpdateDefaultDevice(device->is_input());
}

void AudioDeviceManager::NotifyDeviceGainChanged(const AudioDevice& device) {
  TRACE_DURATION("audio", "AudioDeviceManager::NotifyDeviceGainChanged");
  FX_DCHECK(device.device_settings() != nullptr);
  auto info = device.device_settings()->GetGainInfo();

  for (auto& client : bindings_.bindings()) {
    client->events().OnDeviceGainChanged(device.token(), info);
  }
}

void AudioDeviceManager::UpdateDefaultDevice(bool input) {
  TRACE_DURATION("audio", "AudioDeviceManager::UpdateDefaultDevice");
  const auto new_dev =
      FindLastPlugged(input ? AudioObject::Type::Input : AudioObject::Type::Output);
  uint64_t new_id = new_dev ? new_dev->token() : ZX_KOID_INVALID;
  uint64_t& old_id = input ? default_input_token_ : default_output_token_;

  if (old_id != new_id) {
    FX_LOGS(INFO) << "Default " << (input ? "input" : "output") << " '"
                  << (new_dev ? new_dev->name() : "none") << "'";

    for (auto& client : bindings_.bindings()) {
      client->events().OnDefaultDeviceChanged(old_id, new_id);
    }
    old_id = new_id;
  }
}

void AudioDeviceManager::AddDeviceByChannel(
    std::string device_name, bool is_input,
    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config) {
  TRACE_DURATION("audio", "AudioDeviceManager::AddDeviceByChannel");
  FX_LOGS(INFO) << __FUNCTION__ << (is_input ? ": Input '" : ": Output '") << device_name << "'";

  // Hand the stream off to the proper type of class to manage.
  std::shared_ptr<AudioDevice> new_device;
  if (is_input) {
    new_device =
        AudioInput::Create(device_name, process_config_.device_config(), std::move(stream_config),
                           &threading_model(), this, &link_matrix_, clock_factory_);
  } else {
    new_device = std::make_shared<DriverOutput>(device_name, process_config_.device_config(),
                                                process_config_.mix_profile_config(),
                                                &threading_model(), this, std::move(stream_config),
                                                &link_matrix_, clock_factory_, effects_loader_v2_);
  }

  if (new_device == nullptr) {
    FX_LOGS(ERROR) << "Failed to instantiate audio " << (is_input ? "input" : "output") << " for '"
                   << device_name << "'";
  }

  AddDevice(std::move(new_device));
}

}  // namespace media::audio
