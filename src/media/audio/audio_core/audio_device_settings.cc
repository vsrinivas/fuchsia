// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings.h"

#include <lib/zx/clock.h>

#include <fbl/auto_lock.h>
#include <trace/event.h>

#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {

// static
fbl::RefPtr<AudioDeviceSettings> AudioDeviceSettings::Create(const AudioDriver& drv,
                                                             bool is_input) {
  return fbl::MakeRefCounted<AudioDeviceSettings>(drv.persistent_unique_id(), drv.hw_gain_state(),
                                                  is_input);
}

AudioDeviceSettings::AudioDeviceSettings(const audio_stream_unique_id_t& uid, const HwGainState& hw,
                                         bool is_input)
    : uid_(uid), is_input_(is_input), can_mute_(hw.can_mute), can_agc_(hw.can_agc) {
  gain_state_.gain_db = hw.cur_gain;
  gain_state_.muted = hw.can_mute && hw.cur_mute;
  gain_state_.agc_enabled = hw.can_agc && hw.cur_agc;
}

void AudioDeviceSettings::InitFromClone(const AudioDeviceSettings& other) {
  TRACE_DURATION("audio", "AudioDeviceSettings::InitFromClone");
  FXL_DCHECK(memcmp(&uid_, &other.uid_, sizeof(uid_)) == 0);

  // Clone the gain settings.
  fuchsia::media::AudioGainInfo gain_info;
  other.GetGainInfo(&gain_info);
  SetGainInfo(gain_info, UINT32_MAX);

  // Clone misc. flags.
  ignored_ = other.Ignored();
  auto_routing_disabled_ = other.AutoRoutingDisabled();
}

bool AudioDeviceSettings::SetGainInfo(const fuchsia::media::AudioGainInfo& req,
                                      uint32_t set_flags) {
  TRACE_DURATION("audio", "AudioDeviceSettings::SetGainInfo");
  fbl::AutoLock lock(&settings_lock_);
  audio_set_gain_flags_t dirtied = gain_state_dirty_flags_;

  if ((set_flags & fuchsia::media::SetAudioGainFlag_GainValid) &&
      (gain_state_.gain_db != req.gain_db)) {
    gain_state_.gain_db = req.gain_db;
    dirtied = static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_GAIN_VALID);
  }

  bool mute_tgt = (req.flags & fuchsia::media::AudioGainInfoFlag_Mute) != 0;
  if ((set_flags & fuchsia::media::SetAudioGainFlag_MuteValid) && (gain_state_.muted != mute_tgt)) {
    gain_state_.muted = mute_tgt;
    dirtied = static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_MUTE_VALID);
  }

  bool agc_tgt = (req.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled) != 0;
  if ((set_flags & fuchsia::media::SetAudioGainFlag_AgcValid) &&
      (gain_state_.agc_enabled != agc_tgt)) {
    gain_state_.agc_enabled = agc_tgt;
    dirtied = static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_AGC_VALID);
  }

  bool needs_wake = (!gain_state_dirty_flags_ && dirtied);
  gain_state_dirty_flags_ = dirtied;

  if (needs_wake) {
    NotifyObserver();
  }

  return needs_wake;
}

void AudioDeviceSettings::GetGainInfo(fuchsia::media::AudioGainInfo* out_info) const {
  TRACE_DURATION("audio", "AudioDeviceSettings::GetGainInfo");
  FXL_DCHECK(out_info != nullptr);

  // TODO(35439): consider eliminating the acquisition of this lock.  In theory, the only mutation
  // of gain state happens during SetGainInfo, which is supposed to only be called from the
  // AudioDeviceManager, which should be functionally single threaded as it is called only from the
  // main service message loop. Since GetGainInfo should only be called from the device manager as
  // well, we should not need the settings_lock_ to observe the gain state from this method.
  //
  // Conversely, if we had an efficient reader/writer lock, we should only need to obtain this lock
  // for read which should always succeed without contention.
  fbl::AutoLock lock(&settings_lock_);

  out_info->gain_db = gain_state_.gain_db;
  out_info->flags = 0;

  if (can_mute_ && gain_state_.muted) {
    out_info->flags |= fuchsia::media::AudioGainInfoFlag_Mute;
  }

  if (can_agc_) {
    out_info->flags |= fuchsia::media::AudioGainInfoFlag_AgcSupported;
    if (gain_state_.agc_enabled) {
      out_info->flags |= fuchsia::media::AudioGainInfoFlag_AgcEnabled;
    }
  }
}

audio_set_gain_flags_t AudioDeviceSettings::SnapshotGainState(GainState* out_state) {
  TRACE_DURATION("audio", "AudioDeviceSettings::SnapshotGainState");
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

void AudioDeviceSettings::SetIgnored(bool ignored) {
  fbl::AutoLock lock(&settings_lock_);
  if (ignored != ignored_) {
    ignored_ = ignored;
    NotifyObserver();
  }
}

void AudioDeviceSettings::SetAutoRoutingDisabled(bool auto_routing_disabled) {
  fbl::AutoLock lock(&settings_lock_);
  if (auto_routing_disabled != auto_routing_disabled_) {
    auto_routing_disabled_ = auto_routing_disabled;
    NotifyObserver();
  }
}

void AudioDeviceSettings::NotifyObserver() {
  if (observer_) {
    observer_(this);
  }
}

}  // namespace media::audio
