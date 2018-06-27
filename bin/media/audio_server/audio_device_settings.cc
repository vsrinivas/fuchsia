// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <fcntl.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lib/fxl/files/directory.h"

#include "garnet/bin/media/audio_server/audio_device_settings.h"
#include "garnet/bin/media/audio_server/audio_driver.h"
#include "garnet/bin/media/audio_server/schema/audio_device_settings_schema.inl"

namespace media {
namespace audio {

namespace {
constexpr size_t kMaxSettingFileSize = (64 << 10);
constexpr uint32_t kAllSetGainFlags =
    ::fuchsia::media::SetAudioGainFlag_GainValid |
    ::fuchsia::media::SetAudioGainFlag_MuteValid |
    ::fuchsia::media::SetAudioGainFlag_AgcValid;

std::ostream& operator<<(std::ostream& stream,
                         const rapidjson::ParseResult& result) {
  return stream << "(offset " << result.Offset() << " : "
                << rapidjson::GetParseError_En(result.Code()) << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const rapidjson::SchemaValidator::ValueType& value) {
  // clang-format off
  if (value.IsNull()) return stream << "<null>";
  if (value.IsBool()) return stream << value.GetBool();
  if (value.IsInt()) return stream << value.GetInt();
  if (value.IsUint()) return stream << value.GetUint();
  if (value.IsInt64()) return stream << value.GetInt64();
  if (value.IsUint64()) return stream << value.GetUint64();
  if (value.IsDouble()) return stream << value.GetDouble();
  if (value.IsString()) return stream << "\"" << value.GetString() << "\"";
  if (value.IsFloat()) return stream << value.GetFloat();
  // clang-format on

  if (value.IsArray()) {
    bool first = true;
    stream << "[";
    for (auto iter = value.Begin(); iter != value.End(); ++iter) {
      stream << (first ? "" : ", ") << *iter;
      first = false;
    }
    return stream << "]";
  }

  if (value.IsObject()) {
    bool first = true;
    stream << "{ ";
    for (auto iter = value.MemberBegin(); iter != value.MemberEnd(); ++iter) {
      stream << (first ? "" : ", ") << iter->name << " : " << iter->value;
      first = false;
    }
    return stream << " }";
  }

  return stream << "???";
}

}  // namespace

const std::string AudioDeviceSettings::kSettingsPath =
    "/data/media/audio/settings";
bool AudioDeviceSettings::initialized_ = false;
std::unique_ptr<rapidjson::SchemaDocument> AudioDeviceSettings::file_schema_;

AudioDeviceSettings::AudioDeviceSettings(const AudioDriver& drv, bool is_input)
    : uid_(drv.persistent_unique_id()),
      is_input_(is_input),
      can_mute_(drv.hw_gain_state().can_mute),
      can_agc_(drv.hw_gain_state().can_agc) {
  const auto& hw = drv.hw_gain_state();

  gain_state_.db_gain = hw.cur_gain;
  gain_state_.muted = hw.can_mute && hw.cur_mute;
  gain_state_.agc_enabled = hw.can_agc && hw.cur_agc;
}

void AudioDeviceSettings::Initialize() {
  FXL_DCHECK(!initialized_);
  if (!files::CreateDirectory(kSettingsPath)) {
    FXL_LOG(ERROR)
        << "Failed to ensure that \"" << kSettingsPath
        << "\" exists!  Settings will neither be persisted nor restored.";
    return;
  }

  rapidjson::Document schema_doc;
  rapidjson::ParseResult parse_res =
      schema_doc.Parse(kAudioDeviceSettingsSchema.c_str());
  if (parse_res.IsError()) {
    FXL_LOG(ERROR) << "Failed to parse settings file JSON schema " << parse_res
                   << "!  Settings will neither be persisted nor restored.";
    return;
  }

  initialized_ = true;
  file_schema_ = std::make_unique<rapidjson::SchemaDocument>(schema_doc);
}

zx_status_t AudioDeviceSettings::InitFromDisk() {
  // Don't bother to do any of this unless we were able to successfully
  // initialize our storage subsystem.
  if (!initialized_) {
    return ZX_ERR_BAD_STATE;
  }

  // Start by attempting to open a pre-existing file which has our settings in
  // it.  If we cannot find such a file, or if the file exists but is invalid,
  // simply create a new file and write out our current settings.
  char path[256];
  {
    const uint8_t* x = uid_.data;
    snprintf(
        path, sizeof(path),
        "%s/"
        "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x-%s."
        "json",
        kSettingsPath.c_str(), x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7],
        x[8], x[9], x[10], x[11], x[12], x[13], x[14], x[15],
        is_input_ ? "input" : "output");
  }

  FXL_DCHECK(static_cast<bool>(storage_) == false);
  storage_.reset(::open(path, O_RDWR));

  if (static_cast<bool>(storage_)) {
    zx_status_t res = Deserialize();
    if (res == ZX_OK) {
      CancelCommitTimeouts();
      return ZX_OK;
    }

    FXL_LOG(WARNING) << "Failed to deserialize audio settings file \"" << path
                     << "\" (res " << res
                     << ").  Re-creating file from defaults.";
    storage_.reset();
    ::unlink(path);
  }

  storage_.reset(::open(path, O_RDWR | O_CREAT));
  if (!static_cast<bool>(storage_)) {
    FXL_LOG(ERROR) << "Failed to create new audio settings file \"" << path
                   << "\" (errno " << errno
                   << ").  Settings will not be persisted.";
    return ZX_ERR_IO;
  }

  // We have failed to load our existing settings for one reason or another, but
  // we do have a file we can write to.  Create a new file from our current
  // default settings.
  zx_status_t res = Serialize();
  if (res != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to serialize audio settings file \"" << path
                     << "\" (res " << res
                     << ").  Settings will not be persisted.";
    storage_.reset();
    ::unlink(path);
  }

  return res;
}

void AudioDeviceSettings::InitFromClone(const AudioDeviceSettings& other) {
  FXL_DCHECK(::memcmp(&uid_, &other.uid_, sizeof(uid_)) == 0);

  // Clone the gain settings.
  ::fuchsia::media::AudioGainInfo gain_info;
  other.GetGainInfo(&gain_info);
  SetGainInfo(gain_info, kAllSetGainFlags);

  // Clone misc. flags.
  ignore_device_ = other.ignore_device();
  disallow_auto_routing_ = other.disallow_auto_routing();
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

  if (needs_wake) {
    UpdateCommitTimeouts();
  }

  return needs_wake;
}

zx::time AudioDeviceSettings::Commit(bool force) {
  // If we are not backed by storage, or the cache is clean, then there is
  // nothing to commit.
  if (!static_cast<bool>(storage_) ||
      (next_commit_time_ == zx::time::infinite())) {
    return zx::time::infinite();
  }

  zx::time now;
  zx::clock::get(&now);

  if (force || (now >= next_commit_time_)) {
    Serialize();
  }

  return next_commit_time_;
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

zx_status_t AudioDeviceSettings::Deserialize() {
  if (!static_cast<bool>(storage_)) {
    return ZX_ERR_NOT_FOUND;
  }

  // Figure out the size of the file, then allocate storage for reading the
  // whole thing.
  off_t file_size = ::lseek(storage_.get(), 0, SEEK_END);
  if ((file_size <= 0) ||
      (static_cast<size_t>(file_size) > kMaxSettingFileSize)) {
    return ZX_ERR_BAD_STATE;
  }

  if (::lseek(storage_.get(), 0, SEEK_SET) != 0) {
    return ZX_ERR_IO;
  }

  // Allocate the buffer and read in the contents.
  auto buffer = std::make_unique<char[]>(file_size + 1);
  if (::read(storage_.get(), buffer.get(), file_size) != file_size) {
    return ZX_ERR_IO;
  }
  buffer[file_size] = 0;

  // Parse the contents
  rapidjson::Document doc;
  rapidjson::ParseResult parse_res = doc.ParseInsitu(buffer.get());
  if (parse_res.IsError()) {
    FXL_LOG(WARNING) << "Parse error " << parse_res
                     << " when reading persisted audio settings.";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Validate that the document conforms to our schema
  FXL_DCHECK(file_schema_ != nullptr);
  rapidjson::SchemaValidator validator(*file_schema_);
  if (!doc.Accept(validator)) {
    FXL_LOG(WARNING)
        << "Schema validation error when reading persisted audio settings.";
    FXL_LOG(WARNING) << "Error: " << validator.GetError();
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Extract the gain information
  ::fuchsia::media::AudioGainInfo gain_info;
  const auto& gain_obj = doc["gain"].GetObject();
  gain_info.db_gain = gain_obj["db_gain"].GetDouble();

  if (gain_obj["mute"].GetBool()) {
    gain_info.flags |= ::fuchsia::media::AudioGainInfoFlag_Mute;
  }

  if (gain_obj["agc"].GetBool()) {
    gain_info.flags |= ::fuchsia::media::AudioGainInfoFlag_AgcEnabled;
  }

  // Apply gain settings.
  SetGainInfo(gain_info, kAllSetGainFlags);

  // Extract misc. flags.
  ignore_device_ = doc["ignore_device"].GetBool();
  disallow_auto_routing_ = doc["disallow_auto_routing"].GetBool();

  // Success!
  return ZX_OK;
}  // namespace audio

zx_status_t AudioDeviceSettings::Serialize() {
  CancelCommitTimeouts();

  if (!static_cast<bool>(storage_)) {
    return ZX_ERR_NOT_FOUND;
  }

  // Serialize our state into a string buffer.
  rapidjson::StringBuffer buffer(nullptr, 4096);
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  ::fuchsia::media::AudioGainInfo gain_info;

  GetGainInfo(&gain_info);
  writer.StartObject();
  writer.Key("gain");
  writer.StartObject();
  writer.Key("db_gain");
  writer.Double(gain_info.db_gain);
  writer.Key("mute");
  writer.Bool(gain_info.flags & ::fuchsia::media::AudioGainInfoFlag_Mute);
  writer.Key("agc");
  writer.Bool(
      (gain_info.flags & ::fuchsia::media::AudioGainInfoFlag_AgcEnabled) &&
      (gain_info.flags & ::fuchsia::media::AudioGainInfoFlag_AgcSupported));
  writer.EndObject();  // end "gain" object
  writer.Key("ignore_device");
  writer.Bool(ignore_device());
  writer.Key("disallow_auto_routing");
  writer.Bool(disallow_auto_routing());
  writer.EndObject();  // end top level object

  // Truncate the file down to nothing, write the data, and finally flush the
  // file.
  //
  // TODO(johngro): We should really double buffer these settings files in case
  // of power loss.  Even better would be to have a fuchsia service which
  // manages storing and updating settings in a transactional and reliable
  // fashion along with other features like rate limiting of updates.
  const char* data = buffer.GetString();
  const size_t sz = buffer.GetSize();
  if ((::lseek(storage_.get(), 0, SEEK_SET) != 0) ||
      (::ftruncate(storage_.get(), 0) != 0) ||
      (::write(storage_.get(), data, sz) != static_cast<ssize_t>(sz)) ||
      (::fsync(storage_.get()) != 0)) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

void AudioDeviceSettings::UpdateCommitTimeouts() {
  constexpr zx::duration kMaxUpdateDelay(ZX_SEC(5));
  constexpr zx::duration kUpdateDelay(ZX_MSEC(500));

  zx::time now;
  zx::clock::get(&now);
  if (max_commit_time_ == zx::time::infinite()) {
    max_commit_time_ = now + kMaxUpdateDelay;
  }

  next_commit_time_ = std::min(now + kUpdateDelay, max_commit_time_);
}

void AudioDeviceSettings::CancelCommitTimeouts() {
  next_commit_time_ = zx::time::infinite();
  max_commit_time_ = zx::time::infinite();
}

}  // namespace audio
}  // namespace media
