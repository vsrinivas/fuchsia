// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings_json.h"

#include <fcntl.h>
#include <lib/zx/clock.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/auto_lock.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <trace/event.h>

#include "src/lib/files/directory.h"
#include "src/media/audio/audio_core/audio_device_settings_json.h"
#include "src/media/audio/audio_core/audio_driver.h"

#include "src/media/audio/audio_core/schema/audio_device_settings_schema.inl"

namespace media::audio {

namespace {
constexpr size_t kMaxSettingFileSize = (64 << 10);

std::ostream& operator<<(std::ostream& stream, const rapidjson::ParseResult& result) {
  return stream << "(offset " << result.Offset() << " : "
                << rapidjson::GetParseError_En(result.Code()) << ")";
}

std::ostream& operator<<(std::ostream& stream, const rapidjson::SchemaValidator::ValueType& value) {
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

// static
zx_status_t AudioDeviceSettingsJson::Create(std::unique_ptr<AudioDeviceSettingsJson>* ptr) {
  return CreateWithSchema(kAudioDeviceSettingsSchema.c_str(), ptr);
}

zx_status_t AudioDeviceSettingsJson::CreateWithSchema(
    const char* schema, std::unique_ptr<AudioDeviceSettingsJson>* ptr) {
  TRACE_DURATION("audio", "AudioDeviceSettingsJson::CreateWithSchema");

  rapidjson::Document schema_doc;
  rapidjson::ParseResult parse_res = schema_doc.Parse(schema);
  if (parse_res.IsError()) {
    FXL_LOG(ERROR) << "Failed to parse settings file JSON schema " << parse_res << "!";
    return ZX_ERR_INVALID_ARGS;
  }
  *ptr = std::unique_ptr<AudioDeviceSettingsJson>(
      new AudioDeviceSettingsJson(rapidjson::SchemaDocument(std::move(schema_doc))));
  return ZX_OK;
}

zx_status_t AudioDeviceSettingsJson::Deserialize(int fd, AudioDeviceSettings* settings) {
  TRACE_DURATION("audio", "AudioDeviceSettingsJson::Deserialize");
  FXL_DCHECK(settings != nullptr);
  FXL_DCHECK(fd >= 0);

  // Figure out the size of the file, then allocate storage for reading the whole thing.
  off_t file_size = lseek(fd, 0, SEEK_END);
  if ((file_size <= 0) || (static_cast<size_t>(file_size) > kMaxSettingFileSize)) {
    return ZX_ERR_BAD_STATE;
  }
  if (lseek(fd, 0, SEEK_SET) != 0) {
    return ZX_ERR_IO;
  }

  // Allocate the buffer and read in the contents.
  auto buffer = std::make_unique<char[]>(file_size + 1);
  if (read(fd, buffer.get(), file_size) != file_size) {
    return ZX_ERR_IO;
  }
  buffer[file_size] = 0;

  // Parse the contents
  rapidjson::Document doc;
  rapidjson::ParseResult parse_res = doc.ParseInsitu(buffer.get());
  if (parse_res.IsError()) {
    FXL_LOG(WARNING) << "Parse error " << parse_res << " when reading persisted audio settings.";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Validate that the document conforms to our schema
  rapidjson::SchemaValidator validator(schema_);
  if (!doc.Accept(validator)) {
    FXL_LOG(WARNING) << "Schema validation error when reading persisted audio settings.";
    FXL_LOG(WARNING) << "Error: " << validator.GetError();
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Extract the gain information
  fuchsia::media::AudioGainInfo gain_info;
  const auto& gain_obj = doc["gain"].GetObject();

  gain_info.gain_db = static_cast<float>(gain_obj["gain_db"].GetDouble());

  if (gain_obj["mute"].GetBool()) {
    gain_info.flags |= fuchsia::media::AudioGainInfoFlag_Mute;
  }

  if (gain_obj["agc"].GetBool()) {
    gain_info.flags |= fuchsia::media::AudioGainInfoFlag_AgcEnabled;
  }

  // Apply gain settings.
  settings->SetGainInfo(gain_info, UINT32_MAX);

  // Extract misc. flags.
  settings->SetIgnored(doc["ignore_device"].GetBool());
  settings->SetAutoRoutingDisabled(doc["disallow_auto_routing"].GetBool());

  return ZX_OK;
}

zx_status_t AudioDeviceSettingsJson::Serialize(int fd, const AudioDeviceSettings& settings) {
  TRACE_DURATION("audio", "AudioDeviceSettingsJson::Serialize");
  FXL_DCHECK(fd >= 0);

  // Serialize our state into a string buffer.
  rapidjson::StringBuffer buffer(nullptr, 4096);
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  {
    // Write gain info.
    fuchsia::media::AudioGainInfo gain_info;
    settings.GetGainInfo(&gain_info);
    writer.StartObject();
    writer.Key("gain");
    writer.StartObject();
    writer.Key("gain_db");
    writer.Double(gain_info.gain_db);
    writer.Key("mute");
    writer.Bool(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
    writer.Key("agc");
    writer.Bool((gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled) &&
                (gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported));
    writer.EndObject();  // end "gain" object
  }

  writer.Key("ignore_device");
  writer.Bool(settings.Ignored());
  writer.Key("disallow_auto_routing");
  writer.Bool(settings.AutoRoutingDisabled());
  writer.EndObject();  // end top level object

  const char* data = buffer.GetString();
  const size_t sz = buffer.GetSize();
  if ((lseek(fd, 0, SEEK_SET) != 0)) {
    FXL_LOG(ERROR) << "Failed to seek: " << strerror(errno);
    return ZX_ERR_IO;
  }
  if (ftruncate(fd, 0) != 0) {
    FXL_LOG(ERROR) << "Failed to truncate: " << strerror(errno);
    return ZX_ERR_IO;
  }
  if (write(fd, data, sz) != static_cast<ssize_t>(sz)) {
    FXL_LOG(ERROR) << "Failed to write: " << strerror(errno);
    return ZX_ERR_IO;
  }
  // Some filesystems do not support sync; allow for that and continue.
  if (fsync(fd != 0) && errno != ENOTSUP) {
    FXL_LOG(ERROR) << "Failed to sync: " << strerror(errno) << ", " << errno;
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

}  // namespace media::audio
