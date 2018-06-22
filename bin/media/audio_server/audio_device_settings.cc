// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include "garnet/bin/media/audio_server/audio_device_settings.h"
#include "garnet/bin/media/audio_server/audio_driver.h"

namespace media {
namespace audio {

AudioDeviceSettings::AudioDeviceSettings(const AudioDriver& drv)
    : uid_(drv.persistent_unique_id()),
      can_mute_(drv.hw_gain_state().can_mute),
      can_agc_(drv.hw_gain_state().can_agc) {
  const auto& hw = drv.hw_gain_state();

  gain_state_.db_gain = hw.cur_gain;
  gain_state_.muted = hw.can_mute && hw.cur_mute;
  gain_state_.agc_enabled = hw.can_agc && hw.cur_agc;
}

bool AudioDeviceSettings::SetGainInfo(
    const ::fuchsia::media::AudioGainInfo& req, uint32_t set_flags) {
  fbl::AutoLock lock(&settings_lock_);
  audio_set_gain_flags_t dirtied = gain_state_dirty_flags_;
  namespace fm = ::fuchsia::media;

  if ((set_flags & fm::SetAudioGainFlag_GainValid) &&
      (gain_state_.db_gain != req.db_gain)) {
    gain_state_.db_gain = req.db_gain;
    dirtied =
        static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_GAIN_VALID);
  }

  bool mute_tgt = (req.flags & fm::AudioGainInfoFlag_Mute) != 0;
  if ((set_flags & fm::SetAudioGainFlag_MuteValid) &&
      (gain_state_.muted != mute_tgt)) {
    gain_state_.muted = mute_tgt;
    dirtied =
        static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_MUTE_VALID);
  }

  bool agc_tgt = (req.flags & fm::AudioGainInfoFlag_AgcEnabled) != 0;
  if ((set_flags & fm::SetAudioGainFlag_AgcValid) &&
      (gain_state_.agc_enabled != agc_tgt)) {
    gain_state_.agc_enabled = agc_tgt;
    dirtied =
        static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_AGC_VALID);
  }

  bool needs_wake = (!gain_state_dirty_flags_ && dirtied);
  gain_state_dirty_flags_ = dirtied;
  return needs_wake;
}

void AudioDeviceSettings::GetGainInfo(
    ::fuchsia::media::AudioGainInfo* out_info) const {
  FXL_DCHECK(out_info != nullptr);

  // TODO(johngro): consider eliminating the acquisition of this lock.  In
  // theory, the only mutation of gain state happens during SetGainInfo, which
  // is supposed to only be called from the AudioDeviceManager, which should be
  // functionally single threaded as it is called only from the main service
  // message loop. Since GetGainInfo should only be called from the device
  // manager as well, we should not need the settings_lock_ to observe the
  // gain state from this method.
  //
  // Conversely, if we had an efficient reader/writer lock, we should only need
  // to obtain this lock for read which should always succeed without
  // contention.
  fbl::AutoLock lock(&settings_lock_);

  out_info->db_gain = gain_state_.db_gain;
  out_info->flags = 0;

  if (can_mute_ && gain_state_.muted) {
    out_info->flags |= ::fuchsia::media::AudioGainInfoFlag_Mute;
  }

  if (can_agc_) {
    out_info->flags |= ::fuchsia::media::AudioGainInfoFlag_AgcSupported;
    if (gain_state_.agc_enabled) {
      out_info->flags |= ::fuchsia::media::AudioGainInfoFlag_AgcEnabled;
    }
  }
}

audio_set_gain_flags_t AudioDeviceSettings::SnapshotGainState(
    GainState* out_state) {
  FXL_DCHECK(out_state != nullptr);
  audio_set_gain_flags_t ret;

  {
    fbl::AutoLock lock(&settings_lock_);
    *out_state = gain_state_;
    ret = gain_state_dirty_flags_;
    gain_state_dirty_flags_ = static_cast<audio_set_gain_flags_t>(0);
  }

  return ret;
}

}  // namespace audio
}  // namespace media
