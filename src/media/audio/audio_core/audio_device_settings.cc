// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings.h"

#include <lib/trace/event.h>
#include <lib/zx/clock.h>

#include <cstdint>

#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {

AudioDeviceSettings::AudioDeviceSettings(const audio_stream_unique_id_t& uid, const HwGainState& hw,
                                         bool is_input)
    : uid_(uid), is_input_(is_input), can_agc_(hw.can_agc) {
  gain_state_.gain_db = hw.cur_gain;
  gain_state_.muted = hw.cur_mute;
  gain_state_.agc_enabled = hw.can_agc && hw.cur_agc;
}

AudioDeviceSettings::AudioDeviceSettings(const AudioDeviceSettings& o)
    : uid_(o.uid_), is_input_(o.is_input_), can_agc_(o.can_agc_) {
  std::lock_guard<std::mutex> lock(o.settings_lock_);
  gain_state_.gain_db = o.gain_state_.gain_db;
  gain_state_.muted = o.gain_state_.muted;
  gain_state_.agc_enabled = o.gain_state_.agc_enabled;
}

bool AudioDeviceSettings::SetGainInfo(const fuchsia::media::AudioGainInfo& req,
                                      fuchsia::media::AudioGainValidFlags set_flags) {
  TRACE_DURATION("audio", "AudioDeviceSettings::SetGainInfo");
  std::lock_guard<std::mutex> lock(settings_lock_);
  audio_set_gain_flags_t dirtied = gain_state_dirty_flags_;

  if (((set_flags & fuchsia::media::AudioGainValidFlags::GAIN_VALID) ==
       fuchsia::media::AudioGainValidFlags::GAIN_VALID) &&
      (gain_state_.gain_db != req.gain_db)) {
    gain_state_.gain_db = req.gain_db;
    dirtied = static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_GAIN_VALID);
  }

  bool mute_tgt = (req.flags & fuchsia::media::AudioGainInfoFlags::MUTE) ==
                  fuchsia::media::AudioGainInfoFlags::MUTE;
  if (((set_flags & fuchsia::media::AudioGainValidFlags::MUTE_VALID) ==
       fuchsia::media::AudioGainValidFlags::MUTE_VALID) &&
      (gain_state_.muted != mute_tgt)) {
    gain_state_.muted = mute_tgt;
    dirtied = static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_MUTE_VALID);
  }

  bool agc_tgt = (req.flags & fuchsia::media::AudioGainInfoFlags::AGC_ENABLED) ==
                 fuchsia::media::AudioGainInfoFlags::AGC_ENABLED;
  if (((set_flags & fuchsia::media::AudioGainValidFlags::AGC_VALID) ==
       fuchsia::media::AudioGainValidFlags::AGC_VALID) &&
      (gain_state_.agc_enabled != agc_tgt)) {
    gain_state_.agc_enabled = agc_tgt;
    dirtied = static_cast<audio_set_gain_flags_t>(dirtied | AUDIO_SGF_AGC_VALID);
  }

  bool needs_wake = (!gain_state_dirty_flags_ && dirtied);
  gain_state_dirty_flags_ = dirtied;

  return needs_wake;
}

fuchsia::media::AudioGainInfo AudioDeviceSettings::GetGainInfo() const {
  TRACE_DURATION("audio", "AudioDeviceSettings::GetGainInfo");

  // TODO(fxbug.dev/35439): consider eliminating the acquisition of this lock.  In theory, the only
  // mutation of gain state happens during SetGainInfo, which is supposed to only be called from the
  // AudioDeviceManager, which should be functionally single threaded as it is called only from the
  // main service message loop. Since GetGainInfo should only be called from the device manager as
  // well, we should not need the settings_lock_ to observe the gain state from this method.
  //
  // Conversely, if we had an efficient reader/writer lock, we should only need to obtain this lock
  // for read which should always succeed without contention.
  std::lock_guard<std::mutex> lock(settings_lock_);

  fuchsia::media::AudioGainInfoFlags flags = {};

  if (gain_state_.muted) {
    flags |= fuchsia::media::AudioGainInfoFlags::MUTE;
  }

  if (can_agc_) {
    flags |= fuchsia::media::AudioGainInfoFlags::AGC_SUPPORTED;
    if (gain_state_.agc_enabled) {
      flags |= fuchsia::media::AudioGainInfoFlags::AGC_ENABLED;
    }
  }

  return {
      .gain_db = gain_state_.gain_db,
      .flags = flags,
  };
}

std::pair<audio_set_gain_flags_t, AudioDeviceSettings::GainState>
AudioDeviceSettings::SnapshotGainState() {
  TRACE_DURATION("audio", "AudioDeviceSettings::SnapshotGainState");

  std::lock_guard<std::mutex> lock(settings_lock_);
  audio_set_gain_flags_t flags = gain_state_dirty_flags_;
  gain_state_dirty_flags_ = static_cast<audio_set_gain_flags_t>(0);

  return {flags, gain_state_};
}

}  // namespace media::audio
