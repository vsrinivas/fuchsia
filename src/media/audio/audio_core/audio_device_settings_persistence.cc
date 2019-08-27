// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings_persistence.h"

#include <fcntl.h>
#include <lib/zx/clock.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/auto_lock.h>
#include <trace/event.h>

#include "src/lib/files/directory.h"
#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {

namespace {

const std::string kSettingsPath = "/data/settings";
const std::string kDefaultSettingsPath = "/config/data/settings/default";

static const AudioDeviceSettingsPersistence::ConfigSource kDefaultConfigSources[2] = {
    {.prefix = kSettingsPath, .is_default = false},
    {.prefix = kDefaultSettingsPath, .is_default = true},
};

}  // namespace

AudioDeviceSettingsPersistence::AudioDeviceSettingsPersistence(async_dispatcher_t* dispatcher)
    : AudioDeviceSettingsPersistence(dispatcher, kDefaultConfigSources) {}

AudioDeviceSettingsPersistence::AudioDeviceSettingsPersistence(
    async_dispatcher_t* dispatcher,
    const AudioDeviceSettingsPersistence::ConfigSource (&configs)[2])
    : configs_(configs), dispatcher_(dispatcher) {
  // We expect one default one non-default config path.
  FXL_DCHECK(configs_[0].is_default != configs_[1].is_default);
  FXL_DCHECK(dispatcher_);
}

// static
void AudioDeviceSettingsPersistence::Initialize() {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::Initialize");
  for (const auto& cfg_src : configs_) {
    if (!cfg_src.is_default) {
      if (!files::CreateDirectory(cfg_src.prefix)) {
        FXL_LOG(ERROR) << "Failed to ensure that \"" << cfg_src.prefix
                       << "\" exists!  Settings will neither be persisted nor restored.";
      }
    }
  }

  zx_status_t status = AudioDeviceSettingsJson::Create(&json_);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to initialize device settings JSON";
    return;
  }
  FXL_DCHECK(json_);
}

void AudioDeviceSettingsPersistence::CancelPendingWriteback() { commit_settings_task_.Cancel(); }

zx_status_t AudioDeviceSettingsPersistence::LoadSettings(
    const fbl::RefPtr<AudioDeviceSettings>& settings) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::LoadSettings");
  FXL_DCHECK(settings != nullptr);
  AudioDeviceSettingsHolder holder;
  holder.settings = settings;
  auto [it, inserted] = persisted_device_settings_.insert({settings.get(), std::move(holder)});
  if (inserted) {
    zx_status_t status = ReadSettingsFromDisk(&it->second);
    if (status != ZX_OK) {
      persisted_device_settings_.erase(it);
      return status;
    }
    settings->set_observer([this](auto* settings) {
      auto it = persisted_device_settings_.find(settings);
      if (it != persisted_device_settings_.end() && writes_enabled_) {
        UpdateCommitTimeouts(&it->second);
      }
    });
    return ZX_OK;
  } else {
    const uint8_t* id = settings->uid().data;
    char id_buf[33];
    std::snprintf(id_buf, sizeof(id_buf),
                  "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", id[0], id[1],
                  id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12],
                  id[13], id[14], id[15]);
    FXL_LOG(WARNING) << "Warning: Device shares a persistent unique ID (" << id_buf
                     << ") with another device in the system.  Initial Settings will be cloned "
                        "from this device, and not persisted";
    settings->InitFromClone(*it->first);
    return ZX_OK;
  }
}

zx_status_t AudioDeviceSettingsPersistence::ReadSettingsFromDisk(
    AudioDeviceSettingsHolder* holder) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::ReadSettingsFromDisk");
  FXL_DCHECK(static_cast<bool>(holder->storage) == false);

  for (const auto& cfg_src : configs_) {
    // Start by attempting to open a pre-existing file which has our settings in
    // it. If we cannot find such a file, or if the file exists but is invalid,
    // simply create a new file and write out our current settings.
    char path[256];
    fbl::unique_fd storage;

    CreateSettingsPath(*holder->settings, cfg_src.prefix, path, sizeof(path));
    storage.reset(open(path, cfg_src.is_default ? O_RDONLY : O_RDWR));

    if (storage) {
      zx_status_t res = json_->Deserialize(storage.get(), holder->settings.get());
      if (res == ZX_OK) {
        if (cfg_src.is_default) {
          // If we just loaded and deserialized the fallback default config,
          // break out of the loop and fall thru to serialization code.
          break;
        }

        // We successfully loaded our persisted settings. Cancel our commit
        // timer, hold onto the FD, and we should be good to go.
        CancelCommitTimeouts(holder);
        holder->storage = std::move(storage);
        return ZX_OK;
      } else {
        holder->storage.reset();
        if (!cfg_src.is_default) {
          FXL_PLOG(INFO, res) << "Failed to read device settings at \"" << path
                              << "\". Re-creating file from defaults";
          unlink(path);
        } else {
          FXL_PLOG(INFO, res) << "Could not load default audio settings file \"" << path << "\"";
        }
      }
    }
  }

  // If persisting of device settings is disabled, don't create a new file.
  if (!writes_enabled_) {
    return ZX_OK;
  }

  FXL_DCHECK(configs_[0].is_default || configs_[1].is_default);
  const std::string& writable_settings_path =
      configs_[0].is_default ? configs_[1].prefix : configs_[0].prefix;

  // We failed to load persisted settings for one reason or another.
  // Create a new settings file for this device; persist our defaults there.
  char path[256];
  CreateSettingsPath(*holder->settings, writable_settings_path, path, sizeof(path));
  FXL_DCHECK(!holder->storage);
  holder->storage.reset(open(path, O_RDWR | O_CREAT));

  if (!holder->storage) {
    // TODO(mpuryear): define and enforce a limit for the number of settings
    // files allowed to be created.
    FXL_LOG(WARNING) << "Failed to create new audio settings file \"" << path << "\" (err " << errno
                     << "). Settings for this device will not be persisted.";
    return ZX_ERR_IO;
  }

  zx_status_t res = json_->Serialize(holder->storage.get(), *holder->settings);
  if (res != ZX_OK) {
    FXL_PLOG(WARNING, res) << "Failed to write new settings file at \"" << path
                           << "\". Settings for this device will not be persisted";
    holder->storage.reset();
    unlink(path);
  }

  return res;
}

zx::time AudioDeviceSettingsPersistence::Commit(const AudioDeviceSettingsHolder& holder,
                                                bool force) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::Commit");
  // If 1) we aren't backed by storage, or 2) device settings persistence is disabled, or 3) the
  // cache is clean, then there is nothing to commit.
  if (!holder.storage || !writes_enabled_ || (holder.next_commit_time == zx::time::infinite())) {
    return zx::time::infinite();
  }

  zx::time now;
  zx::clock::get(&now);

  if (force || (now >= holder.next_commit_time)) {
    json_->Serialize(holder.storage.get(), *holder.settings);
  }

  return holder.next_commit_time;
}

void AudioDeviceSettingsPersistence::UpdateCommitTimeouts(AudioDeviceSettingsHolder* holder) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::UpdateCommitTimeouts");
  if (!writes_enabled_) {
    FXL_LOG(DFATAL) << "Device settings files disabled; we should not be here.";
  }

  constexpr zx::duration kMaxUpdateDelay(ZX_SEC(5));
  constexpr zx::duration kUpdateDelay(ZX_MSEC(500));

  zx::time now;
  zx::clock::get(&now);
  if (holder->max_commit_time == zx::time::infinite()) {
    holder->max_commit_time = now + kMaxUpdateDelay;
  }

  holder->next_commit_time = std::min(now + kUpdateDelay, holder->max_commit_time);
}

void AudioDeviceSettingsPersistence::CancelCommitTimeouts(AudioDeviceSettingsHolder* holder) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::CancelCommitTimeouts");
  holder->next_commit_time = zx::time::infinite();
  holder->max_commit_time = zx::time::infinite();
}

void AudioDeviceSettingsPersistence::CreateSettingsPath(const AudioDeviceSettings& settings,
                                                        const std::string& prefix, char* out_path,
                                                        size_t out_path_len) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::CreateSettingsPath");
  const uint8_t* x = settings.uid().data;
  snprintf(out_path, out_path_len,
           "%s/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x-%s.json",
           prefix.c_str(), x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10], x[11],
           x[12], x[13], x[14], x[15], settings.is_input() ? "input" : "output");
}

void AudioDeviceSettingsPersistence::CommitDirtySettings() {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::CommitDirtySettings");
  zx::time next = zx::time::infinite();

  for (auto& settings : persisted_device_settings_) {
    zx::time tmp = Commit(settings.second);
    if (tmp < next) {
      next = tmp;
    }
  }

  // If our commit task is waiting to fire, try to cancel it.
  if (commit_settings_task_.is_pending()) {
    commit_settings_task_.Cancel();
  }

  // If we need to update in the future, schedule a commit task to do so.
  if (next != zx::time::infinite()) {
    commit_settings_task_.PostForTime(dispatcher_, next);
  }
}

void AudioDeviceSettingsPersistence::FinalizeDeviceSettings(const AudioDeviceSettings& settings) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::FinalizeDeviceSettings");
  auto it = persisted_device_settings_.find(&settings);
  if (it != persisted_device_settings_.end()) {
    Commit(it->second, true);
    persisted_device_settings_.erase(it);
  } else {
    FXL_LOG(INFO) << "Not found!";
  }
}

}  // namespace media::audio
