// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_device.h"

#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "garnet/bin/media/audio_server/audio_link.h"
#include "garnet/bin/media/audio_server/audio_output.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace media {
namespace audio {

namespace {
std::string AudioDeviceUniqueIdToString(const audio_stream_unique_id_t& id) {
  static_assert(sizeof(id.data) == 16, "Unexpected unique ID size");
  char buf[(sizeof(id.data) * 2) + 1];

  const auto& d = id.data;
  snprintf(buf, sizeof(buf),
           "%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x",
           d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10],
           d[11], d[12], d[13], d[14], d[15]);
  return std::string(buf, sizeof(buf) - 1);
}
}  // namespace

AudioDevice::AudioDevice(AudioObject::Type type, AudioDeviceManager* manager)
    : AudioObject(type), manager_(manager), driver_(new AudioDriver(this)) {
  FXL_DCHECK(manager_);
  FXL_DCHECK((type == Type::Input) || (type == Type::Output));
}

AudioDevice::~AudioDevice() { FXL_DCHECK(is_shutting_down()); }

void AudioDevice::Wakeup() {
  FXL_DCHECK(mix_wakeup_ != nullptr);
  mix_wakeup_->Signal();
}

uint64_t AudioDevice::token() const {
  return driver_ ? driver_->stream_channel_koid() : ZX_KOID_INVALID;
}

zx_status_t AudioDevice::Init() {
  // TODO(johngro) : See MG-940.  Eliminate this priority boost as soon as we
  // have a more official way of meeting real-time latency requirements.
  mix_domain_ = ::dispatcher::ExecutionDomain::Create(24);
  mix_wakeup_ = ::dispatcher::WakeupEvent::Create();

  if ((mix_domain_ == nullptr) || (mix_wakeup_ == nullptr)) {
    return ZX_ERR_NO_MEMORY;
  }

  ::dispatcher::WakeupEvent::ProcessHandler process_handler(
      [output = fbl::WrapRefPtr(this)](
          ::dispatcher::WakeupEvent* event) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
        output->OnWakeup();
        return ZX_OK;
      });

  zx_status_t res =
      mix_wakeup_->Activate(mix_domain_, fbl::move(process_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate wakeup event for AudioDevice!  "
                   << "(res " << res << ")";
    return res;
  }

  return ZX_OK;
}

void AudioDevice::Cleanup() {}

void AudioDevice::ActivateSelf() {
  // If we are not shutting down, send a message to the device manager letting
  // it know that we are ready to do some work.
  if (!is_shutting_down()) {
    // clang-format off
    FXL_DCHECK(manager_);
    manager_->ScheduleMainThreadTask(
      [ manager = manager_, self = fbl::WrapRefPtr(this) ]() {
        manager->ActivateDevice(self);
      });
    // clang-format on
  }
}

void AudioDevice::ShutdownSelf() {
  // If we are not already in the process of shutting down, send a message to
  // the main message loop telling it to complete the shutdown process.
  if (!is_shutting_down()) {
    PreventNewLinks();

    FXL_DCHECK(mix_domain_);
    mix_domain_->DeactivateFromWithinDomain();

    FXL_DCHECK(manager_);
    manager_->ScheduleMainThreadTask(
        [manager = manager_, self = fbl::WrapRefPtr(this)]() {
          manager->RemoveDevice(self);
        });
  }
}

void AudioDevice::DeactivateDomain() {
  if (mix_domain_ != nullptr) {
    mix_domain_->Deactivate();
  }
}

zx_status_t AudioDevice::Startup() {
  // If our derived class failed to initialize, Just get out.  We are being
  // called by the output manager, and they will remove us from the set of
  // active outputs as a result of us failing to initialize.
  zx_status_t res = Init();
  if (res != ZX_OK) {
    DeactivateDomain();
    return res;
  }

  // Poke the output once so it gets a chance to actually start running.
  Wakeup();

  return ZX_OK;
}

void AudioDevice::Shutdown() {
  if (shut_down_) {
    return;
  }

  // Make sure no new callbacks can be generated, and that pending callbacks
  // have been nerfed.
  DeactivateDomain();

  // Unlink ourselves from everything we are currently attached to.
  Unlink();

  // Give our derived class a chance to clean up its resources.
  Cleanup();

  // We are now completely shut down.  The only reason we have this flag is to
  // make sure that Shutdown is idempotent.
  shut_down_ = true;
}

bool AudioDevice::UpdatePlugState(bool plugged, zx_time_t plug_time) {
  if ((plugged != plugged_) && (plug_time >= plug_time_)) {
    plugged_ = plugged;
    plug_time_ = plug_time;
    return true;
  }

  return false;
}

const fbl::RefPtr<DriverRingBuffer>& AudioDevice::driver_ring_buffer() const {
  return driver_->ring_buffer();
};

const TimelineFunction& AudioDevice::driver_clock_mono_to_ring_pos_bytes()
    const {
  return driver_->clock_mono_to_ring_pos_bytes();
};

audio_set_gain_flags_t AudioDevice::SnapshotGainState(GainState* out_state) {
  FXL_DCHECK(out_state != nullptr);
  audio_set_gain_flags_t ret;

  {
    fbl::AutoLock lock(&gain_state_lock_);
    *out_state = gain_state_;
    ret = gain_state_dirty_flags_;
    gain_state_dirty_flags_ = static_cast<audio_set_gain_flags_t>(0);
  }

  return ret;
}

void AudioDevice::SetGainInfo(const ::fuchsia::media::AudioGainInfo& req,
                              uint32_t set_flags) {
  bool needs_wake;

  fuchsia::media::AudioGainInfo info = req;
  ApplyGainLimits(&info, set_flags);

  {
    fbl::AutoLock lock(&gain_state_lock_);
    audio_set_gain_flags_t dirtied = static_cast<audio_set_gain_flags_t>(0);
    namespace fm = ::fuchsia::media;

    if ((set_flags & fm::SetAudioGainFlag_GainValid) &&
        (gain_state_.db_gain != info.db_gain)) {
      gain_state_.db_gain = info.db_gain;
      dirtied =
          static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_GAIN_VALID);
    }

    bool mute_tgt = (info.flags & fm::AudioGainInfoFlag_Mute) != 0;
    if ((set_flags & fm::SetAudioGainFlag_MuteValid) &&
        (gain_state_.muted != mute_tgt)) {
      gain_state_.muted = mute_tgt;
      dirtied =
          static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_MUTE_VALID);
    }

    bool agc_tgt = (info.flags & fm::AudioGainInfoFlag_AgcEnabled) != 0;
    if ((set_flags & fm::SetAudioGainFlag_AgcValid) &&
        (gain_state_.agc_enabled != agc_tgt)) {
      gain_state_.agc_enabled = agc_tgt;
      dirtied =
          static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_AGC_VALID);
    }

    needs_wake = (!gain_state_dirty_flags_ && dirtied);
    gain_state_dirty_flags_ = dirtied;
  }

  if (needs_wake) {
    Wakeup();
  }
}

void AudioDevice::GetGainInfo(::fuchsia::media::AudioGainInfo* out_info) const {
  FXL_DCHECK(out_info != nullptr);
  fbl::AutoLock lock(&gain_state_lock_);

  out_info->db_gain = gain_state_.db_gain;
  out_info->flags = 0;

  if (gain_state_.muted) {
    out_info->flags |= ::fuchsia::media::AudioGainInfoFlag_Mute;
  }

  if (driver()->hw_gain_state().can_agc) {
    out_info->flags |= ::fuchsia::media::AudioGainInfoFlag_AgcSupported;
    if (gain_state_.agc_enabled) {
      out_info->flags |= ::fuchsia::media::AudioGainInfoFlag_AgcEnabled;
    }
  }
}

void AudioDevice::GetDeviceInfo(
    ::fuchsia::media::AudioDeviceInfo* out_info) const {
  const auto& drv = *driver();
  out_info->name = drv.manufacturer_name() + ' ' + drv.product_name();
  out_info->unique_id = AudioDeviceUniqueIdToString(drv.persistent_unique_id());
  out_info->token_id = token();
  out_info->is_input = is_input();
  out_info->is_default = false;

  GetGainInfo(&out_info->gain_info);
}

}  // namespace audio
}  // namespace media
