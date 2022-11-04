// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/policy_loader.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <string>

#include <fbl/unique_fd.h>
#include <rapidjson/error/en.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/media/audio/audio_core/v1/schema/audio_policy_schema.inl"

namespace media::audio {

namespace {
static constexpr size_t kMaxSettingFileSize = (64 << 10);
static const std::string kPolicyPath = "/config/data/audio_policy.json";

static const std::string kIdleCountdownMsKey = "idle_countdown_milliseconds";
static const std::string kStartupCountdownMsKey = "startup_idle_countdown_milliseconds";
static const std::string kUltrasonicChannelsKey = "use_all_ultrasonic_channels";

std::optional<fuchsia::media::AudioRenderUsage> JsonToRenderUsage(const rapidjson::Value& usage) {
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
    FX_LOGS(ERROR) << usage.GetString() << " not a valid AudioRenderUsage.";
  }

  return std::nullopt;
}

std::optional<fuchsia::media::AudioCaptureUsage> JsonToCaptureUsage(const rapidjson::Value& usage) {
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
    FX_LOGS(ERROR) << usage.GetString() << " not a valid AudioCaptureUsage.";
  }

  return std::nullopt;
}

std::optional<fuchsia::media::Behavior> JsonToBehavior(const rapidjson::Value& behavior) {
  auto behavior_str = behavior.GetString();

  if (!strcmp(behavior_str, "NONE")) {
    return fuchsia::media::Behavior::NONE;
  } else if (!strcmp(behavior_str, "DUCK")) {
    return fuchsia::media::Behavior::DUCK;
  } else if (!strcmp(behavior_str, "MUTE")) {
    return fuchsia::media::Behavior::MUTE;
  } else {
    FX_LOGS(ERROR) << behavior_str << " not a valid Behavior.";
  }

  return std::nullopt;
}

static std::optional<fuchsia::media::Usage> JsonToUsage(const rapidjson::Value& usage) {
  fuchsia::media::Usage ret;
  if (usage.HasMember("render_usage")) {
    auto u = JsonToRenderUsage(usage["render_usage"]);
    if (!u) {
      return std::nullopt;
    }
    return fuchsia::media::Usage::WithRenderUsage(std::move(*u));
  }

  if (usage.HasMember("capture_usage")) {
    auto u = JsonToCaptureUsage(usage["capture_usage"]);
    if (!u) {
      return std::nullopt;
    }
    return fuchsia::media::Usage::WithCaptureUsage(std::move(*u));
  }

  return std::nullopt;
}

}  // namespace

fpromise::result<AudioPolicy> PolicyLoader::ParseConfig(const char* file_body) {
  rapidjson::Document doc;

  std::vector<AudioPolicy::Rule> rules;
  rapidjson::ParseResult parse_res = doc.Parse<rapidjson::kParseIterativeFlag>(file_body);
  if (parse_res.IsError()) {
    FX_LOGS(ERROR) << "Failed to parse settings file JSON schema: "
                   << rapidjson::GetParseError_En(parse_res.Code()) << " " << parse_res.Offset()
                   << file_body + parse_res.Offset();
    return fpromise::error();
  }

  rapidjson::Document doc2;
  parse_res = doc2.Parse(kAudioPolicySchema);
  if (parse_res.IsError()) {
    FX_LOGS(ERROR) << "Failed to parse settings file JSON schema: "
                   << rapidjson::GetParseError_En(parse_res.Code()) << " " << parse_res.Offset()
                   << kAudioPolicySchema + parse_res.Offset();
    return fpromise::error();
  }

  rapidjson::SchemaDocument schema_doc(doc2);
  rapidjson::SchemaValidator validator(schema_doc);

  if (!doc.Accept(validator)) {
    FX_LOGS(ERROR) << "Schema validation error when reading policy settings.";

    return fpromise::error();
  }

  const rapidjson::Value& rules_json = doc["audio_policy_rules"];
  if (!rules_json.IsArray()) {
    return fpromise::error();
  }
  for (auto& rule_json : rules_json.GetArray()) {
    auto& rule = rules.emplace_back();
    if (!rule_json.IsObject()) {
      return fpromise::error();
    }
    if (rule_json.HasMember("active")) {
      auto active = JsonToUsage(rule_json["active"]);
      if (!active) {
        FX_LOGS(ERROR) << "Rule `active` object invalid.";
        return fpromise::error();
      }
      rule.active = std::move(*active);
    } else {
      FX_LOGS(ERROR) << "Rule `active` object missing.";
      return fpromise::error();
    }

    if (rule_json.HasMember("affected")) {
      auto affected = JsonToUsage(rule_json["affected"]);
      if (!affected) {
        FX_LOGS(ERROR) << "Rule `affected` object invalid.";
        return fpromise::error();
      }
      rule.affected = std::move(*affected);
    } else {
      FX_LOGS(ERROR) << "Rule `affected` object missing.";
      return fpromise::error();
    }

    if (rule_json.HasMember("behavior")) {
      auto behavior = JsonToBehavior(rule_json["behavior"]);
      if (!behavior) {
        FX_LOGS(ERROR) << "Rule `behavior` object invalid.";
        return fpromise::error();
      }
      rule.behavior = std::move(*behavior);
    } else {
      FX_LOGS(ERROR) << "Rule `behavior` object missing.";
      return fpromise::error();
    }
  }

  AudioPolicy::IdlePowerOptions options;
  if (!ParseIdlePowerOptions(doc, options)) {
    return fpromise::error();
  }

  FX_LOGS(INFO) << "Successfully loaded " << rules.size() << " rules, plus policy options";

  return fpromise::ok(AudioPolicy{std::move(rules), options});
}

bool PolicyLoader::ParseIdlePowerOptions(rapidjson::Document& doc,
                                         AudioPolicy::IdlePowerOptions& options) {
  if (!doc.HasMember(kIdleCountdownMsKey)) {
    FX_LOGS(INFO) << "'" << kIdleCountdownMsKey << "' is missing; not enacting idle-power policy";
    if (doc.HasMember(kStartupCountdownMsKey)) {
      FX_LOGS(WARNING) << "'" << kStartupCountdownMsKey << "' will be ignored";
    }
    if (doc.HasMember(kUltrasonicChannelsKey)) {
      FX_LOGS(WARNING) << "'" << kUltrasonicChannelsKey << "' will be ignored";
    }
    return true;
  }
  options.idle_countdown_duration = zx::msec(doc[kIdleCountdownMsKey].GetInt64());

  if (doc.HasMember(kStartupCountdownMsKey)) {
    options.startup_idle_countdown_duration = zx::msec(doc[kStartupCountdownMsKey].GetInt64());
  }

  if (doc.HasMember(kUltrasonicChannelsKey)) {
    options.use_all_ultrasonic_channels = doc[kUltrasonicChannelsKey].GetBool();
  }

  return true;
}

fpromise::result<AudioPolicy, zx_status_t> PolicyLoader::LoadConfigFromFile(
    const std::string filename) {
  fbl::unique_fd json_file;
  json_file.reset(open(filename.c_str(), O_RDONLY));

  if (!json_file.is_valid()) {
    return fpromise::error(ZX_ERR_NOT_FOUND);
  }

  // Figure out the size of the file, then allocate storage for reading the whole thing.
  off_t file_size = lseek(json_file.get(), 0, SEEK_END);
  if ((file_size <= 0)) {
    FX_LOGS(ERROR) << "Could not find filesize";
    return fpromise::error(ZX_ERR_NOT_FILE);
  }

  if (static_cast<size_t>(file_size) > kMaxSettingFileSize) {
    FX_LOGS(ERROR) << "Config file too large. Max file size: " << kMaxSettingFileSize
                   << " Config file size: " << file_size;
    return fpromise::error(ZX_ERR_FILE_BIG);
  }

  if (lseek(json_file.get(), 0, SEEK_SET) != 0) {
    FX_LOGS(ERROR) << "Failed to seek to 0.";
    return fpromise::error(ZX_ERR_IO);
  }

  // Allocate the buffer and read in the contents.
  auto buffer = std::make_unique<char[]>(file_size + 1);
  if (read(json_file.get(), buffer.get(), file_size) != file_size) {
    FX_LOGS(ERROR) << "Failed to read buffer.";
    return fpromise::error(ZX_ERR_NO_MEMORY);
  }
  buffer[file_size] = 0;

  auto result = PolicyLoader::ParseConfig(buffer.get());
  if (result.is_error()) {
    return fpromise::error(ZX_ERR_NOT_SUPPORTED);
  }
  return fpromise::ok(result.take_value());
}

AudioPolicy PolicyLoader::LoadPolicy() {
  auto result = LoadConfigFromFile(kPolicyPath);
  if (result.is_ok()) {
    return std::move(result.value());
  }

  if (result.error() == ZX_ERR_NOT_FOUND) {
    FX_LOGS(INFO) << "No audio policy found; using default.";
  } else if (result.error() == ZX_ERR_NOT_SUPPORTED) {
    FX_LOGS(INFO) << "Audio policy '" << kPolicyPath
                  << "' loaded but could not be parsed; using default.";
  } else {
    FX_LOGS(WARNING) << "Audio policy '" << kPolicyPath << "' failed to load (err "
                     << result.error() << "); using default.";
  }

  return AudioPolicy();
}

}  // namespace media::audio
