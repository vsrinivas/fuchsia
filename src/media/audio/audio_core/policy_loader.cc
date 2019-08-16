// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/policy_loader.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <fbl/unique_fd.h>
#include <rapidjson/error/en.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/media/audio/audio_core/audio_admin.h"

#include "src/media/audio/audio_core/schema/audio_policy_schema.inl"

namespace media {
namespace audio {

namespace {
static constexpr size_t kMaxSettingFileSize = (64 << 10);
static const std::string kDefaultPolicyPath = "/config/data/settings/default/audio_policy.json";
static const std::string kPlatformDefaultPolicyPath =
    "/config/data/settings/default/platform_audio_policy.json";
}  // namespace

// static
std::optional<fuchsia::media::AudioRenderUsage> PolicyLoader::JsonToRenderUsage(
    const rapidjson::Value& usage) {
  static_assert(fuchsia::media::RENDER_USAGE_COUNT == 5,
                "New Render Usage(s) added to fidl without updating config loader");

  auto rule_str = usage.GetString();

  if (!strcmp(rule_str, "BACKGROUND")) {
    return fuchsia::media::AudioRenderUsage::BACKGROUND;
  } else if (!strcmp(rule_str, "MEDIA")) {
    return fuchsia::media::AudioRenderUsage::MEDIA;
  } else if (!strcmp(rule_str, "INTERRUPTION")) {
    return fuchsia::media::AudioRenderUsage::INTERRUPTION;
  } else if (!strcmp(rule_str, "SYSTEM_AGENT")) {
    return fuchsia::media::AudioRenderUsage::SYSTEM_AGENT;
  } else if (!strcmp(rule_str, "COMMUNICATION")) {
    return fuchsia::media::AudioRenderUsage::COMMUNICATION;
  } else {
    FXL_LOG(ERROR) << usage.GetString() << " not a valid AudioRenderUsage.";
  }

  return std::nullopt;
}

// static
std::optional<fuchsia::media::AudioCaptureUsage> PolicyLoader::JsonToCaptureUsage(
    const rapidjson::Value& usage) {
  static_assert(fuchsia::media::CAPTURE_USAGE_COUNT == 4,
                "New Capture Usage(s) added to fidl without updating config loader");

  auto rule_str = usage.GetString();

  if (!strcmp(rule_str, "BACKGROUND")) {
    return fuchsia::media::AudioCaptureUsage::BACKGROUND;
  } else if (!strcmp(rule_str, "FOREGROUND")) {
    return fuchsia::media::AudioCaptureUsage::FOREGROUND;
  } else if (!strcmp(rule_str, "SYSTEM_AGENT")) {
    return fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT;
  } else if (!strcmp(rule_str, "COMMUNICATION")) {
    return fuchsia::media::AudioCaptureUsage::COMMUNICATION;
  } else {
    FXL_LOG(ERROR) << usage.GetString() << " not a valid AudioCaptureUsage.";
  }

  return std::nullopt;
}

// static
std::optional<fuchsia::media::Behavior> PolicyLoader::JsonToBehavior(
    const rapidjson::Value& behavior) {
  auto behavior_str = behavior.GetString();

  if (!strcmp(behavior_str, "NONE")) {
    return fuchsia::media::Behavior::NONE;
  } else if (!strcmp(behavior_str, "DUCK")) {
    return fuchsia::media::Behavior::DUCK;
  } else if (!strcmp(behavior_str, "MUTE")) {
    return fuchsia::media::Behavior::MUTE;
  } else {
    FXL_LOG(ERROR) << behavior_str << " not a valid Behavior.";
  }

  return std::nullopt;
}

static std::optional<fuchsia::media::Usage> JsonToUsage(const rapidjson::Value& usage) {
  fuchsia::media::Usage ret;
  if (usage.HasMember("render_usage")) {
    auto u = PolicyLoader::JsonToRenderUsage(usage["render_usage"]);
    if (!u) {
      return std::nullopt;
    }
    ret.set_render_usage(*u);
    return ret;
  }

  if (usage.HasMember("capture_usage")) {
    auto u = PolicyLoader::JsonToCaptureUsage(usage["capture_usage"]);
    if (!u) {
      return std::nullopt;
    }
    ret.set_capture_usage(*u);
    return ret;
  }

  return std::nullopt;
}

std::optional<rapidjson::Document> PolicyLoader::ParseConfig(const char* file_body) {
  rapidjson::Document doc;

  rapidjson::ParseResult parse_res = doc.Parse(file_body);
  if (parse_res.IsError()) {
    FXL_LOG(ERROR) << "Failed to parse settings file JSON schema: "
                   << rapidjson::GetParseError_En(parse_res.Code()) << " " << parse_res.Offset()
                   << file_body + parse_res.Offset();
    return std::nullopt;
  }

  rapidjson::Document doc2;
  parse_res = doc2.Parse(kAudioPolicySchema.c_str());
  if (parse_res.IsError()) {
    FXL_LOG(ERROR) << "Failed to parse settings file JSON schema: "
                   << rapidjson::GetParseError_En(parse_res.Code()) << " " << parse_res.Offset()
                   << kAudioPolicySchema.c_str() + parse_res.Offset();
    return std::nullopt;
  }

  rapidjson::SchemaDocument schema_doc(doc2);
  rapidjson::SchemaValidator validator(schema_doc);

  if (!doc.Accept(validator)) {
    FXL_LOG(ERROR) << "Schema validation error when reading policy settings.";

    return std::nullopt;
  }

  const rapidjson::Value& rules = doc["audio_policy_rules"];
  if (!rules.IsArray()) {
    return std::nullopt;
  }
  for (auto& rule : rules.GetArray()) {
    if (!rule.IsObject()) {
      return std::nullopt;
    }
    if (rule.HasMember("active")) {
      auto active = JsonToUsage(rule["active"]);
      if (!active) {
        FXL_LOG(ERROR) << "Rule `active` object invalid.";
        return std::nullopt;
      }
    } else {
      FXL_LOG(ERROR) << "Rule `active` object missing.";
      return std::nullopt;
    }

    if (rule.HasMember("affected")) {
      auto affected = JsonToUsage(rule["affected"]);
      if (!affected) {
        FXL_LOG(ERROR) << "Rule `affected` object invalid.";
        return std::nullopt;
      }
    } else {
      FXL_LOG(ERROR) << "Rule `affected` object missing.";
      return std::nullopt;
    }

    if (rule.HasMember("behavior")) {
      auto behavior = JsonToBehavior(rule["behavior"]);
      if (!behavior) {
        FXL_LOG(ERROR) << "Rule `behavior` object invalid.";
        return std::nullopt;
      }
    } else {
      FXL_LOG(ERROR) << "Rule `behavior` object missing.";
      return std::nullopt;
    }
  }

  FXL_LOG(INFO) << "Successfully loaded " << rules.Size() << " rules.";

  return doc;
}

// static
zx_status_t PolicyLoader::LoadConfig(AudioAdmin* audio_admin, const char* file_body) {
  auto doc = ParseConfig(file_body);

  if (!doc) {
    FXL_LOG(ERROR) << "Failed to parse config.";
    return ZX_ERR_INVALID_ARGS;
  }

  const rapidjson::Value& rules = (*doc)["audio_policy_rules"];
  for (auto& rule : rules.GetArray()) {
    auto active = JsonToUsage(rule["active"]);
    auto affected = JsonToUsage(rule["affected"]);
    auto behavior = JsonToBehavior(rule["behavior"]);
    if (active && affected && behavior) {
      audio_admin->SetInteraction(std::move(*active), std::move(*affected), *behavior);
    } else {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return ZX_OK;
}

// static
zx_status_t PolicyLoader::LoadConfigFromFile(AudioAdmin* audio_admin, const std::string config) {
  zx_status_t res = ZX_OK;
  fbl::unique_fd json_file;

  FXL_LOG(INFO) << "Loading " << config;
  json_file.reset(open(config.c_str(), O_RDONLY));

  if (json_file.is_valid()) {
    // Figure out the size of the file, then allocate storage for reading the
    // whole thing.
    off_t file_size = lseek(json_file.get(), 0, SEEK_END);
    if ((file_size <= 0)) {
      FXL_LOG(ERROR) << "Could not find filesize";
      return ZX_ERR_BAD_STATE;
    }

    if (static_cast<size_t>(file_size) > kMaxSettingFileSize) {
      FXL_LOG(ERROR) << "Config file too large. Max file size: " << kMaxSettingFileSize
                     << " Config file size: " << file_size;
      return ZX_ERR_BAD_STATE;
    }

    if (lseek(json_file.get(), 0, SEEK_SET) != 0) {
      FXL_LOG(ERROR) << "Failed to seek to 0.";
      return ZX_ERR_IO;
    }

    // Allocate the buffer and read in the contents.
    auto buffer = std::make_unique<char[]>(file_size + 1);
    if (read(json_file.get(), buffer.get(), file_size) != file_size) {
      FXL_LOG(ERROR) << "Failed to read buffer.";
      return ZX_ERR_IO;
    }
    buffer[file_size] = 0;

    res = LoadConfig(audio_admin, buffer.get());
  } else {
    FXL_LOG(WARNING) << "Failed to load " << config;
    return ZX_ERR_IO;
  }
  return res;
}

void PolicyLoader::LoadDefaults(AudioAdmin* audio_admin) {
  if (LoadConfigFromFile(audio_admin, kPlatformDefaultPolicyPath) != ZX_OK) {
    FXL_LOG(WARNING) << "No platform audio_policy found, using defaults.";
    if (LoadConfigFromFile(audio_admin, kDefaultPolicyPath) != ZX_OK) {
      FXL_LOG(ERROR) << "No default audio_policy found, no policy will be used.";
    }
  }
}

}  // namespace audio
}  // namespace media
