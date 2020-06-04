// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_manager.h"

#include <lib/fit/promise.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/trace/event.h>

#include <string>

#include "src/media/audio/audio_core/audio_core_impl.h"
#include "src/media/audio/audio_core/base_capturer.h"
#include "src/media/audio/audio_core/base_renderer.h"
#include "src/media/audio/audio_core/driver_output.h"
#include "src/media/audio/audio_core/plug_detector.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

AudioDeviceManager::AudioDeviceManager(ThreadingModel& threading_model,
                                       std::unique_ptr<PlugDetector> plug_detector,
                                       RouteGraph& route_graph, LinkMatrix& link_matrix,
                                       ProcessConfig& process_config)
    : threading_model_(threading_model),
      route_graph_(route_graph),
      plug_detector_(std::move(plug_detector)),
      link_matrix_(link_matrix),
      process_config_(process_config) {}

AudioDeviceManager::~AudioDeviceManager() {
  Shutdown();
  FX_DCHECK(devices_.empty());
}

// Configure this admin singleton object to manage audio device instances.
zx_status_t AudioDeviceManager::Init() {
  TRACE_DURATION("audio", "AudioDeviceManager::Init");

  // Start monitoring for plug/unplug events of pluggable audio output devices.
  zx_status_t res =
      plug_detector_->Start(fit::bind_member(this, &AudioDeviceManager::AddDeviceByVersion));
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

  std::vector<fit::promise<void>> device_promises;
  for (auto& [_, device] : devices_pending_init_) {
    device_promises.push_back(device->Shutdown());
  }
  devices_pending_init_.clear();

  for (auto& [_, device] : devices_) {
    device_promises.push_back(device->Shutdown());
  }
  devices_.clear();

  fit::run_single_threaded(fit::join_promise_vector(std::move(device_promises)));
}

void AudioDeviceManager::AddDeviceEnumeratorClient(
    fidl::InterfaceRequest<fuchsia::media::AudioDeviceEnumerator> request) {
  bindings_.AddBinding(this, std::move(request));
}

fit::promise<void, fuchsia::media::audio::UpdateEffectError> AudioDeviceManager::UpdateEffect(
    const std::string& instance_name, const std::string& config) {
  std::vector<fit::promise<void, fuchsia::media::audio::UpdateEffectError>> promises;
  for (auto& [_, device] : devices_) {
    promises.push_back(device->UpdateEffect(instance_name, config));
  }
  return fit::join_promise_vector(std::move(promises))
      .then(
          [](fit::result<std::vector<fit::result<void, fuchsia::media::audio::UpdateEffectError>>>&
                 results) -> fit::result<void, fuchsia::media::audio::UpdateEffectError> {
            FX_DCHECK(results.is_ok()) << "fit::join_promise_vector returns an error";
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
              return fit::ok();
            } else {
              return fit::error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
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
            REPORT(DeviceStartupFailed(*device));
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

  devices_.insert(std::move(dev));

  REPORT(ActivatingDevice(*device));
  device->SetActivated();

  // Notify interested users of the new device.
  auto info = device->GetDeviceInfo();

  // We always report is_default as false in the OnDeviceAdded event. There will be a following
  // DefaultDeviceChange event that will signal if this device is now the default.
  info.is_default = false;

  for (auto& client : bindings_.bindings()) {
    client->events().OnDeviceAdded(info);
  }

  if (device->plugged()) {
    AddDeviceToRouteGraph(device, device->plug_time());
  }
}

void AudioDeviceManager::RemoveDevice(const std::shared_ptr<AudioDevice>& device) {
  TRACE_DURATION("audio", "AudioDeviceManager::RemoveDevice");
  FX_DCHECK(device != nullptr);

  // If device was active: reset the default (based on most-recently-plugged).
  OnPlugStateChanged(device, false, device->plug_time());

  REPORT(RemovingDevice(*device));
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
    return;
  }

  // If the device is not yet activated, we should not be changing routes.
  bool activated = devices_.find(device->token()) != devices_.end();
  if (!activated) {
    return;
  }

  if (plugged) {
    AddDeviceToRouteGraph(device, plug_time);
  } else {
    RemoveDeviceFromRouteGraph(device, plug_time);
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
}  // namespace media::audio

void AudioDeviceManager::SetDeviceGain(uint64_t device_token,
                                       fuchsia::media::AudioGainInfo gain_info,
                                       uint32_t set_flags) {
  TRACE_DURATION("audio", "AudioDeviceManager::SetDeviceGain");
  auto it = devices_.find(device_token);
  if (it == devices_.end()) {
    return;
  }
  auto [_, dev] = *it;

  // SetGainInfo clamps out-of-range values (e.g. +infinity) into the device-
  // allowed gain range. NAN is undefined (signless); handle it here and exit.
  if ((set_flags & fuchsia::media::SetAudioGainFlag_GainValid) && isnan(gain_info.gain_db)) {
    FX_LOGS(WARNING) << "Invalid device gain " << gain_info.gain_db << " dB -- making no change";
    return;
  }

  dev->system_gain_dirty = true;

  // Change the gain and then report the new settings to our clients.
  REPORT(SettingDeviceGainInfo(*dev, gain_info, set_flags));
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

void AudioDeviceManager::RemoveDeviceFromRouteGraph(const std::shared_ptr<AudioDevice>& device,
                                                    zx::time plug_time) {
  TRACE_DURATION("audio", "AudioDeviceManager::RemoveDeviceFromRouteGraph");
  FX_DCHECK(device);

  device->UpdatePlugState(/*plugged=*/false, plug_time);

  route_graph_.RemoveDevice(device.get());
  UpdateDefaultDevice(device->is_input());
}

void AudioDeviceManager::AddDeviceToRouteGraph(const std::shared_ptr<AudioDevice>& device,
                                               zx::time plug_time) {
  TRACE_DURATION("audio", "AudioDeviceManager::AddDeviceToRouteGraph");
  FX_DCHECK(device);

  device->UpdatePlugState(/*plugged=*/true, plug_time);

  route_graph_.AddDevice(device.get());
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
    for (auto& client : bindings_.bindings()) {
      client->events().OnDefaultDeviceChanged(old_id, new_id);
    }
    old_id = new_id;
  }
}

void AudioDeviceManager::AddDeviceByVersion(zx::channel device_channel, std::string device_name,
                                            bool is_input, AudioDriverVersion version) {
  switch (version) {
    case AudioDriverVersion::V1:
      AddDeviceByChannel(std::move(device_channel), std::move(device_name), is_input);
      break;
    case AudioDriverVersion::V2:
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config = {};
      stream_config.set_channel(std::move(device_channel));
      AddDeviceByChannel2(std::move(device_name), is_input, std::move(stream_config));
      break;
  }
}

void AudioDeviceManager::AddDeviceByChannel(zx::channel device_channel, std::string device_name,
                                            bool is_input) {
  TRACE_DURATION("audio", "AudioDeviceManager::AddDeviceByChannel");
  AUD_VLOG(TRACE) << " adding " << (is_input ? "input" : "output") << " '" << device_name << "'";

  // Hand the stream off to the proper type of class to manage.
  std::shared_ptr<AudioDevice> new_device;
  if (is_input) {
    new_device =
        AudioInput::Create(std::move(device_channel), &threading_model(), this, &link_matrix_);
  } else {
    new_device =
        std::make_shared<DriverOutput>(&threading_model(), this, std::move(device_channel),
                                       &link_matrix_, process_config_.default_volume_curve());
  }

  if (new_device == nullptr) {
    FX_LOGS(ERROR) << "Failed to instantiate audio " << (is_input ? "input" : "output") << " for '"
                   << device_name << "'";
  }

  REPORT(AddingDevice(device_name, *new_device));
  AddDevice(std::move(new_device));
}

void AudioDeviceManager::AddDeviceByChannel2(
    std::string device_name, bool is_input,
    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config) {
  TRACE_DURATION("audio", "AudioDeviceManager::AddDeviceByChannel2");
  AUD_VLOG(TRACE) << " adding2 " << (is_input ? "input" : "output") << " '" << device_name << "'";

  // Hand the stream off to the proper type of class to manage.
  std::shared_ptr<AudioDevice> new_device;
  if (is_input) {
    new_device =
        AudioInput::Create(std::move(stream_config), &threading_model(), this, &link_matrix_);
  } else {
    new_device =
        std::make_shared<DriverOutput>(&threading_model(), this, std::move(stream_config),
                                       &link_matrix_, process_config_.default_volume_curve());
  }

  if (new_device == nullptr) {
    FX_LOGS(ERROR) << "Failed to instantiate audio " << (is_input ? "input" : "output") << " for '"
                   << device_name << "'";
  }

  REPORT(AddingDevice(device_name, *new_device));
  AddDevice(std::move(new_device));
}

}  // namespace media::audio
