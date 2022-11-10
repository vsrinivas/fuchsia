// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_device.h"

#include <lib/fpromise/bridge.h>
#include <lib/trace/event.h>

#include "src/media/audio/audio_core/shared/device_id.h"
#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/audio_output.h"
#include "src/media/audio/audio_core/v1/logging_flags.h"
#include "src/media/audio/audio_core/v1/utils.h"

namespace media::audio {
namespace {

constexpr float kDefaultDeviceGain = 0.0;

}  // namespace

// Simple accessor here (not in .h) because of forward-declaration issues with AudioDriver
std::shared_ptr<Clock> AudioDevice::reference_clock() {
  FX_DCHECK(driver_);
  return driver_->reference_clock();
}

const DeviceConfig::DeviceProfile& AudioDevice::profile() const {
  if (is_output()) {
    if (!driver_) {
      return config_.default_output_device_profile();
    }
    const auto& device_id = driver_->persistent_unique_id();
    return config_.output_device_profile(device_id);
  } else {
    if (!driver_) {
      return config_.default_input_device_profile();
    }
    const auto& device_id = driver_->persistent_unique_id();
    return config_.input_device_profile(device_id);
  }
}

AudioDevice::AudioDevice(AudioObject::Type type, const std::string& name,
                         const DeviceConfig& config, ThreadingModel* threading_model,
                         DeviceRegistry* registry, LinkMatrix* link_matrix,
                         std::shared_ptr<AudioCoreClockFactory> clock_factory,
                         std::unique_ptr<AudioDriver> driver)
    : AudioObject(type),
      name_(name),
      clock_factory_(clock_factory),
      device_registry_(*registry),
      threading_model_(*threading_model),
      mix_domain_(threading_model->AcquireMixDomain(type == Type::Input ? "input-device"
                                                                        : "output-device")),
      config_(config),
      driver_(std::move(driver)),
      link_matrix_(*link_matrix) {
  FX_DCHECK(registry);
  FX_DCHECK((type == Type::Input) || (type == Type::Output));
  FX_DCHECK(link_matrix);
}

std::optional<Format> AudioDevice::format() const {
  auto _driver = driver();
  if (!_driver) {
    return std::nullopt;
  }
  return _driver->GetFormat();
}

void AudioDevice::Wakeup() {
  TRACE_DURATION("audio", "AudioDevice::Wakeup");
  mix_wakeup_.Signal();
}

uint64_t AudioDevice::token() const {
  return driver_ ? driver_->stream_channel_koid() : ZX_KOID_INVALID;
}

// Change a device's gain, propagating the change to the affected links.
void AudioDevice::SetGainInfo(const fuchsia::media::AudioGainInfo& info,
                              fuchsia::media::AudioGainValidFlags set_flags) {
  TRACE_DURATION("audio", "AudioDevice::SetGainInfo");
  // Limit the request to what the hardware can support
  fuchsia::media::AudioGainInfo limited = info;
  ApplyGainLimits(&limited, set_flags);

  // This is currently implemented with a SW gain.
  SetSoftwareGainInfo(limited);

  FX_DCHECK(device_settings_ != nullptr);
  if (device_settings_->SetGainInfo(limited, set_flags)) {
    Wakeup();
  }
}

void AudioDevice::SetSoftwareGainInfo(const fuchsia::media::AudioGainInfo& info) {
  sw_gain_info_ = info;

  const auto muted = (info.flags & fuchsia::media::AudioGainInfoFlags::MUTE) ==
                     fuchsia::media::AudioGainInfoFlags::MUTE;

  if (is_output()) {
    // See discussion on fxrev.dev/641221.
    if (muted || info.gain_db != 0) {
      FX_LOGS(ERROR) << "Software gain not supported for output devices";
    }
  } else {
    // For inputs, change the gain of all links where it is the source.
    FX_DCHECK(is_input());
    link_matrix_.ForEachDestLink(*this, [&info, muted](LinkMatrix::LinkHandle link) {
      if (link.object->type() == AudioObject::Type::AudioCapturer) {
        if constexpr (kLogSetDeviceGainMuteActions) {
          if (muted) {
            FX_LOGS(WARNING) << "Source device is muted";
          } else {
            // TODO(fxbug.dev/51049) Logging should be removed upon creation of inspect tool or
            // other real-time method for gain observation
            FX_LOGS(INFO) << "Source device gain=" << info.gain_db;
          }
        }
        link.mixer->gain.SetSourceMute(muted);
        link.mixer->gain.SetSourceGain(info.gain_db);
      }
    });
  }
}

void AudioDevice::OnLinkAdded() {
  // Ensure SW gain is set on all new links.
  if (sw_gain_info_) {
    SetSoftwareGainInfo(*sw_gain_info_);
  }
}

zx_status_t AudioDevice::Init() {
  TRACE_DURATION("audio", "AudioDevice::Init");
  WakeupEvent::ProcessHandler process_handler(
      [weak_output = weak_from_this()](WakeupEvent* event) -> zx_status_t {
        auto output = weak_output.lock();
        if (output) {
          OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &output->mix_domain());
          output->OnWakeup();
        }
        return ZX_OK;
      });

  zx_status_t res = mix_wakeup_.Activate(mix_domain_->dispatcher(), std::move(process_handler));
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to activate wakeup event for AudioDevice";
    return res;
  }

  return ZX_OK;
}

void AudioDevice::Cleanup() {
  TRACE_DURATION("audio", "AudioDevice::Cleanup");
  mix_wakeup_.Deactivate();
  // ThrottleOutput devices have no driver, so check for that.
  if (driver_ != nullptr) {
    // Instruct the driver to release all its resources (channels, timer).
    driver_->Cleanup();
  }
  mix_domain_ = nullptr;
}

void AudioDevice::ActivateSelf() {
  TRACE_DURATION("audio", "AudioDevice::ActivateSelf");
  // If we aren't shutting down, tell DeviceManager we are ready for work.
  if (!is_shutting_down()) {
    // Create default settings. The device manager will restore these settings
    // from persistent storage for us when it gets our activation message.
    FX_DCHECK(device_settings_ == nullptr);
    FX_DCHECK(driver() != nullptr);

    HwGainState gain_state = driver()->hw_gain_state();

    // We disregard the device's gain at the time of connection and set it to 0,
    // pending restoration of device_settings.
    gain_state.cur_gain = kDefaultDeviceGain;

    const auto id = driver()->persistent_unique_id();

    device_settings_ = fbl::MakeRefCounted<AudioDeviceSettings>(id, gain_state, is_input());

    // Now poke our manager.
    threading_model().FidlDomain().PostTask(
        [self = shared_from_this()]() { self->device_registry().ActivateDevice(std::move(self)); });
  }
}

void AudioDevice::ShutdownSelf() {
  TRACE_DURATION("audio", "AudioDevice::ShutdownSelf");
  // If we are not already in the process of shutting down, send a message to
  // the main message loop telling it to complete the shutdown process.
  if (!is_shutting_down()) {
    shutting_down_.store(true);

    threading_model().FidlDomain().PostTask(
        [self = shared_from_this()]() { self->device_registry().RemoveDevice(self); });
  }
}

fpromise::promise<void, zx_status_t> AudioDevice::Startup() {
  TRACE_DURATION("audio", "AudioDevice::Startup");
  fpromise::bridge<void, zx_status_t> bridge;
  mix_domain_->PostTask(
      [self = shared_from_this(), completer = std::move(bridge.completer)]() mutable {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &self->mix_domain());
        zx_status_t res = self->Init();
        if (res != ZX_OK) {
          self->Cleanup();
          completer.complete_error(res);
          return;
        }
        self->OnWakeup();
        completer.complete_ok();
      });
  return bridge.consumer.promise();
}

fpromise::promise<void> AudioDevice::Shutdown() {
  TRACE_DURATION("audio", "AudioDevice::Shutdown");
  // The only reason we have this flag is to make sure that Shutdown is idempotent.
  if (shut_down_) {
    return fpromise::make_ok_promise();
  }
  shut_down_ = true;

  // Give our derived class, and our driver, a chance to clean up resources.
  fpromise::bridge<void> bridge;
  mix_domain_->PostTask(
      [self = shared_from_this(), completer = std::move(bridge.completer)]() mutable {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &self->mix_domain());
        self->Cleanup();
        completer.complete_ok();
      });
  return bridge.consumer.promise();
}

bool AudioDevice::UpdatePlugState(bool plugged, zx::time plug_time) {
  if constexpr (kLogAudioDevice || kLogDevicePlugUnplug) {
    FX_LOGS(INFO) << "AudioDevice::" << __FUNCTION__ << ": " << (plugged ? "PLUGGED" : "UNPLUGGED")
                  << " (" << (is_output() ? "output" : "input") << " device " << this << ")";
  }

  TRACE_DURATION("audio", "AudioDevice::UpdatePlugState");
  if ((plugged != plugged_) && (plug_time >= plug_time_)) {
    plugged_ = plugged;
    plug_time_ = plug_time;
    return true;
  }

  return false;
}

void AudioDevice::UpdateRoutableState(bool routable) {
  if constexpr (kLogAudioDevice || kLogRoutingChanges) {
    FX_LOGS(INFO) << "AudioDevice::" << __FUNCTION__ << ": "
                  << (routable ? "ROUTABLE" : "UNROUTABLE") << " (device " << this << ")";
  }

  TRACE_INSTANT("audio", "AudioDevice::UpdateRoutableState", TRACE_SCOPE_PROCESS, "Routable",
                routable);
  routable_ = routable;
}

const std::shared_ptr<ReadableRingBuffer>& AudioDevice::driver_readable_ring_buffer() const {
  return driver_->readable_ring_buffer();
}

const std::shared_ptr<WritableRingBuffer>& AudioDevice::driver_writable_ring_buffer() const {
  return driver_->writable_ring_buffer();
}

const TimelineFunction& AudioDevice::driver_ref_time_to_frac_presentation_frame() const {
  return driver()->ref_time_to_frac_presentation_frame();
}

const TimelineFunction& AudioDevice::driver_ref_time_to_frac_safe_read_or_write_frame() const {
  return driver()->ref_time_to_frac_safe_read_or_write_frame();
}

fuchsia::media::AudioDeviceInfo AudioDevice::GetDeviceInfo() const {
  TRACE_DURATION("audio", "AudioDevice::GetDeviceInfo");

  FX_DCHECK(device_settings_);

  if constexpr (kLogAudioDevice) {
    FX_LOGS(INFO) << "AudioDevice::" << __FUNCTION__ << " (" << (is_input() ? "input " : "output ")
                  << this << "): '" << DeviceUniqueIdToString(driver()->persistent_unique_id())
                  << "', token " << token();
  }
  return {
      .name = driver()->manufacturer_name() + ' ' + driver()->product_name(),
      .unique_id = DeviceUniqueIdToString(driver()->persistent_unique_id()),
      .token_id = token(),
      .is_input = is_input(),
      .gain_info = device_settings_->GetGainInfo(),
      .is_default = false,
  };
}

}  // namespace media::audio
