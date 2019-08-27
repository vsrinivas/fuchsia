// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_PERSISTENCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_PERSISTENCE_H_

#include <lib/async/cpp/task.h>

#include <unordered_map>

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>

#include "src/media/audio/audio_core/audio_device_settings_json.h"

namespace media::audio {

class AudioDeviceSettingsPersistence {
 public:
  explicit AudioDeviceSettingsPersistence(async_dispatcher_t* dispatcher);

  struct ConfigSource {
    const std::string& prefix;
    bool is_default;
  };

  // Construct an |AudioDeviceSettingsPersistence| with custom config sources. Primarily intended
  // for testing purposes. Production use cases should be able to use the single-arg constructor
  // which uses the default config sources.
  //
  // The array reference must be guaranteed to outlive this instance as an internal reference will
  // be retained.
  AudioDeviceSettingsPersistence(async_dispatcher_t* dispatcher,
                                 const AudioDeviceSettingsPersistence::ConfigSource (&configs)[2]);

  // Disallow copy and move.
  AudioDeviceSettingsPersistence(const AudioDeviceSettingsPersistence&) = delete;
  AudioDeviceSettingsPersistence(AudioDeviceSettingsPersistence&&) = delete;
  AudioDeviceSettingsPersistence& operator=(const AudioDeviceSettingsPersistence&) = delete;
  AudioDeviceSettingsPersistence& operator=(AudioDeviceSettingsPersistence&&) = delete;

  void Initialize();
  void CancelPendingWriteback();
  void EnableDeviceSettings(bool enabled) { writes_enabled_ = enabled; }

  zx_status_t LoadSettings(const fbl::RefPtr<AudioDeviceSettings>& settings);

  // Commit any dirty settings to storage, (re)scheduling the timer as needed.
  void CommitDirtySettings();

  void FinalizeDeviceSettings(const AudioDeviceSettings& settings);

 private:
  struct AudioDeviceSettingsHolder {
    fbl::RefPtr<AudioDeviceSettings> settings;
    fbl::unique_fd storage;

    // Members which control the dirty/clean status of the settings relative to storage, and which
    // control the Nagle-ish commit limiter.
    //
    // We introduce two absolute timeouts, next_commit_time and max_commit_time. When settings are
    // clean (in sync with storage), both will be infinite. Anytime a change is introduced, the
    // timeouts are updated as follows.
    //
    // 1) If max is infinite, it is set to now + MaxUpdateDelay, otherwise it is unchanged.
    // 2) next gets set to min(now + UpdateDelay, max_commit_time)
    //
    // When now >= next, it is time to commit. The general idea here is to wait a short amount of
    // time before committing the settings to storage, because another change may be arriving very
    // soon. This said, if the settings are constantly changing, they will need to eventually be
    // committed. The UpdateDelay determines the maximum possible rate at which the settings will be
    // committed, while MaxUpdateDelay determines the minimum commit rate in the event that the
    // settings are constantly changing.
    zx::time next_commit_time = zx::time::infinite();
    zx::time max_commit_time = zx::time::infinite();
  };

  zx_status_t ReadSettingsFromDisk(AudioDeviceSettingsHolder* holder);

  // Commit dirty settings to storage if needed. Return the next time we should commit settings, or
  // zx::time::infinite() if settings are now clean and do not need a future commit time.
  zx::time Commit(const AudioDeviceSettingsHolder& holder, bool force = false);

  void CommitDirtySettingsThunk(async_dispatcher_t*, async::TaskBase*, zx_status_t) {
    CommitDirtySettings();
  }
  void UpdateCommitTimeouts(AudioDeviceSettingsHolder* holder);
  void CancelCommitTimeouts(AudioDeviceSettingsHolder* holder);
  void CreateSettingsPath(const AudioDeviceSettings& settings, const std::string& prefix,
                          char* out_path, size_t out_path_len);

  const ConfigSource (&configs_)[2];
  bool writes_enabled_ = true;

  std::unordered_map<const AudioDeviceSettings*, AudioDeviceSettingsHolder>
      persisted_device_settings_;

  async::TaskMethod<AudioDeviceSettingsPersistence,
                    &AudioDeviceSettingsPersistence::CommitDirtySettingsThunk>
      commit_settings_task_{this};

  std::unique_ptr<AudioDeviceSettingsJson> json_;
  async_dispatcher_t* dispatcher_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DEVICE_SETTINGS_PERSISTENCE_H_
