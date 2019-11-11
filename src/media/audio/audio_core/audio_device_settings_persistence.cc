// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings_persistence.h"

#include <fcntl.h>
#include <lib/async/cpp/time.h>
#include <lib/fit/bridge.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <trace/event.h>

#include "src/lib/files/directory.h"
#include "src/media/audio/audio_core/audio_device_settings_serialization_impl.h"
#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {

namespace {

const std::string kSettingsPath = "/data/settings";
const std::string kDefaultSettingsPath = "/config/data/settings/default";

static const AudioDeviceSettingsPersistence::ConfigSource kDefaultConfigSources[2] = {
    {.prefix = kSettingsPath, .is_default = false},
    {.prefix = kDefaultSettingsPath, .is_default = true},
};

void CreateSettingsPath(const AudioDeviceSettings& settings, const std::string& prefix,
                        char* out_path, size_t out_path_len) {
  TRACE_DURATION("audio", "CreateSettingsPath");
  const uint8_t* x = settings.uid().data;
  snprintf(out_path, out_path_len,
           "%s/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x-%s.json",
           prefix.c_str(), x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10], x[11],
           x[12], x[13], x[14], x[15], settings.is_input() ? "input" : "output");
}

}  // namespace

// static
std::unique_ptr<AudioDeviceSettingsSerialization>
AudioDeviceSettingsPersistence::CreateDefaultSettingsSerializer() {
  std::unique_ptr<AudioDeviceSettingsSerialization> result;
  zx_status_t status = AudioDeviceSettingsSerializationImpl::Create(&result);
  FX_DCHECK(status == ZX_OK);
  FX_DCHECK(result);
  return result;
}

AudioDeviceSettingsPersistence::AudioDeviceSettingsPersistence(ThreadingModel* threading_model)
    : AudioDeviceSettingsPersistence(threading_model, CreateDefaultSettingsSerializer(),
                                     kDefaultConfigSources) {}

AudioDeviceSettingsPersistence::AudioDeviceSettingsPersistence(
    ThreadingModel* threading_model,
    std::unique_ptr<AudioDeviceSettingsSerialization> serialization,
    const AudioDeviceSettingsPersistence::ConfigSource (&configs)[2])
    : configs_(configs),
      threading_model_(*threading_model),
      serialization_(std::move(serialization)) {
  // We expect one default one non-default config path.
  FX_DCHECK(configs_[0].is_default != configs_[1].is_default);
  FX_DCHECK(threading_model);
  FX_DCHECK(serialization_);
}

fit::promise<void, zx_status_t> AudioDeviceSettingsPersistence::LoadSettings(
    fbl::RefPtr<AudioDeviceSettings> settings) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::LoadSettings");
  FX_DCHECK(settings != nullptr);
  std::lock_guard<fxl::ThreadChecker> assert_on_main_thread_(thread_checker_);
  return ReadSettingsFromDisk(settings).and_then([this, settings = std::move(settings)](
                                                     fbl::unique_fd& storage) mutable {
    std::lock_guard<fxl::ThreadChecker> assert_on_main_thread_(thread_checker_);
    auto [it, inserted] = persisted_device_settings_.emplace(
        std::piecewise_construct, std::forward_as_tuple(settings.get()),
        std::forward_as_tuple(settings, std::move(storage)));
    if (inserted) {
      // Setup an observer on the settings data structure. When changes are applied, we'll schedule
      // and update to write back to disk.
      settings->set_observer([this, nonce = it->second.nonce](auto* settings) {
        TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::settings_observer");
        std::lock_guard<fxl::ThreadChecker> assert_on_main_thread_(thread_checker_);
        auto it = persisted_device_settings_.find(settings);
        if (it != persisted_device_settings_.end() && writes_enabled_.load()) {
          RescheduleCommitTask(&it->second);
        }
      });
      // This is the handler to actually perform the writeback when the deadline has expired.
      it->second.commit_task.set_handler(
          [this, holder = &it->second](async_dispatcher_t*, async::Task*, zx_status_t) {
            holder->max_commit_time = zx::time::infinite();
            threading_model_.FidlDomain().executor()->schedule_task(
                Commit(holder->settings, holder->storage.get(), holder->nonce));
          });
      return fit::ok();
    } else {
      const uint8_t* id = settings->uid().data;
      char id_buf[33];
      std::snprintf(id_buf, sizeof(id_buf),
                    "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", id[0],
                    id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11],
                    id[12], id[13], id[14], id[15]);
      FX_LOGS(WARNING) << "Warning: Device shares a persistent unique ID (" << id_buf
                       << ") with another device in the system.  Initial Settings will be cloned "
                          "from this device, and not persisted";
      settings->InitFromClone(*it->first);
      return fit::ok();
    }
  });
}

fit::promise<fbl::unique_fd, zx_status_t> AudioDeviceSettingsPersistence::ReadSettingsFromDisk(
    fbl::RefPtr<AudioDeviceSettings> settings) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::ReadSettingsFromDisk");
  TRACE_FLOW_BEGIN("audio", "AudioDeviceSettingsPersistence.read_from_disk", nonce);
  fit::bridge<fbl::unique_fd, zx_status_t> bridge;
  threading_model_.IoDomain().PostTask([this, completer = std::move(bridge.completer),
                                        settings = std::move(settings), nonce]() mutable {
    TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::ReadSettingsFromDisk.thunk");
    TRACE_FLOW_END("audio", "AudioDeviceSettingsPersistence.read_from_disk", nonce);
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &threading_model_.IoDomain());
    completer.complete_or_abandon(ReadSettingsFromDiskBlocking(std::move(settings)));
  });
  return bridge.consumer.promise();
}

fit::result<fbl::unique_fd, zx_status_t>
AudioDeviceSettingsPersistence::ReadSettingsFromDiskBlocking(
    fbl::RefPtr<AudioDeviceSettings> settings) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::ReadSettingsFromDiskBlocking");

  fbl::unique_fd storage;
  for (const auto& cfg_src : configs_) {
    // Start by attempting to open a pre-existing file which has our settings in
    // it. If we cannot find such a file, or if the file exists but is invalid,
    // simply create a new file and write out our current settings.
    char path[256];

    CreateSettingsPath(*settings, cfg_src.prefix, path, sizeof(path));
    storage.reset(open(path, cfg_src.is_default ? O_RDONLY : O_RDWR));

    if (storage) {
      zx_status_t res = serialization_->Deserialize(storage.get(), settings.get());
      if (res == ZX_OK) {
        if (cfg_src.is_default) {
          // If we just loaded and deserialized the fallback default config,
          // break out of the loop and fall thru to serialization code.
          break;
        }
        return fit::ok(std::move(storage));
      } else {
        storage.reset();
        if (!cfg_src.is_default) {
          FX_PLOGS(INFO, res) << "Failed to read device settings at \"" << path
                              << "\". Re-creating file from defaults";
          unlink(path);
        } else {
          FX_PLOGS(INFO, res) << "Could not load default audio settings file \"" << path << "\"";
        }
      }
    }
  }

  // If persisting of device settings is disabled, don't create a new file.
  if (!writes_enabled_.load()) {
    return fit::ok(fbl::unique_fd());
  }

  FX_DCHECK(configs_[0].is_default || configs_[1].is_default);
  const std::string& writable_settings_path =
      configs_[0].is_default ? configs_[1].prefix : configs_[0].prefix;

  // We failed to load persisted settings for one reason or another.
  // Create a new settings file for this device; persist our defaults there.
  char path[256];
  CreateSettingsPath(*settings, writable_settings_path, path, sizeof(path));
  if (!files::CreateDirectory(writable_settings_path)) {
    FX_LOGS(ERROR) << "Failed to ensure that \"" << writable_settings_path
                   << "\" exists!  Settings will neither be persisted nor restored.";
    return fit::error(ZX_ERR_IO);
  }
  storage.reset(open(path, O_RDWR | O_CREAT));

  if (!storage) {
    // TODO(mpuryear): define and enforce a limit for the number of settings
    // files allowed to be created.
    FX_LOGS(WARNING) << "Failed to create new audio settings file \"" << path << "\" (err " << errno
                     << "). Settings for this device will not be persisted.";
    return fit::error(ZX_ERR_IO);
  }

  zx_status_t res = serialization_->Serialize(storage.get(), *settings);
  if (res != ZX_OK) {
    FX_PLOGS(WARNING, res) << "Failed to write new settings file at \"" << path
                           << "\". Settings for this device will not be persisted";
    storage.reset();
    unlink(path);
    return fit::error(res);
  }

  return fit::ok(std::move(storage));
}

fit::promise<void, zx_status_t> AudioDeviceSettingsPersistence::Commit(
    const fbl::RefPtr<AudioDeviceSettings> settings, int fd, trace_async_id_t nonce) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::Commit");
  TRACE_FLOW_END("audio", "AudioDeviceSettingsPersistence.schedule_commit", nonce);
  if (!writes_enabled_.load()) {
    return fit::make_result_promise<void, zx_status_t>(fit::ok());
  }

  // Clone the settings before writeback to snapshot the current state of the settings.
  fit::bridge<void, zx_status_t> bridge;
  TRACE_FLOW_BEGIN("audio", "AudioDeviceSettingsPersistence.commit", nonce);

  threading_model_.IoDomain().PostTask([this, completer = std::move(bridge.completer),
                                        settings = settings->Clone(), fd, nonce]() mutable {
    TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::Commit.thunk");
    TRACE_FLOW_END("audio", "AudioDeviceSettingsPersistence.commit", nonce);
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &threading_model_.IoDomain());
    zx_status_t status = WriteSettingsToFile(std::move(settings), fd);
    if (status == ZX_OK) {
      completer.complete_ok();
    } else {
      completer.complete_error(status);
    }
  });
  return bridge.consumer.promise();
}

zx_status_t AudioDeviceSettingsPersistence::WriteSettingsToFile(
    fbl::RefPtr<AudioDeviceSettings> settings, int fd) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::WriteSettingsToFile");
  return serialization_->Serialize(fd, *settings);
}

void AudioDeviceSettingsPersistence::RescheduleCommitTask(AudioDeviceSettingsHolder* holder) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::RescheduleCommitTask");
  std::lock_guard<fxl::ThreadChecker> assert_on_main_thread_(thread_checker_);
  zx::time now = async::Now(threading_model_.FidlDomain().dispatcher());
  if (holder->max_commit_time == zx::time::infinite()) {
    holder->max_commit_time = now + kMaxUpdateDelay;
  }

  if (!holder->commit_task.is_pending()) {
    TRACE_FLOW_BEGIN("audio", "AudioDeviceSettingsPersistence.schedule_commit", holder->nonce);
  } else {
    TRACE_FLOW_STEP("audio", "AudioDeviceSettingsPersistence.schedule_commit", holder->nonce);
    holder->commit_task.Cancel();
  }

  holder->commit_task.PostForTime(threading_model_.FidlDomain().dispatcher(),
                                  std::min(now + kUpdateDelay, holder->max_commit_time));
}

fit::promise<void, zx_status_t> AudioDeviceSettingsPersistence::FinalizeSettings(
    const AudioDeviceSettings& settings) {
  TRACE_DURATION("audio", "AudioDeviceSettingsPersistence::FinalizeSettings");
  std::lock_guard<fxl::ThreadChecker> assert_on_main_thread_(thread_checker_);
  auto it = persisted_device_settings_.find(&settings);
  if (it == persisted_device_settings_.end() || !it->second.storage ||
      // We need this check because the map uses the device ID for tracking instances, so it is
      // possible that we'll end up with different AudioDeviceSettings instances that would both
      // return a valid iterator from the map despite the pointers being non-equal. For write back
      // we only want to perform the write if we have this pointer equality.
      it->second.settings.get() != &settings) {
    return fit::make_result_promise<void, zx_status_t>(fit::ok());
  }
  // We pass a continuation to take ownership of the fbl::unique_fd to ensure we don't close the
  // fd before the async write back completes.
  auto p = Commit(it->second.settings, it->second.storage.get(), it->second.nonce)
               .inspect([storage = std::move(it->second.storage)](
                            fit::result<void, zx_status_t>& result) {});
  persisted_device_settings_.erase(it);
  return std::move(p);
}

}  // namespace media::audio
