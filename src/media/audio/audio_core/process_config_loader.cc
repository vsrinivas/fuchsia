// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <string_view>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>

#include "rapidjson/prettywriter.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

#include "src/media/audio/audio_core/schema/audio_core_config_schema.inl"

namespace media::audio {

namespace {

static constexpr char kJsonKeyVolumeCurve[] = "volume_curve";
static constexpr char kJsonKeyPipeline[] = "pipeline";
static constexpr char kJsonKeyLib[] = "lib";
static constexpr char kJsonKeyName[] = "name";
static constexpr char kJsonKeyConfig[] = "config";
static constexpr char kJsonKeyStreams[] = "streams";
static constexpr char kJsonKeyEffects[] = "effects";
static constexpr char kJsonKeyOutputStreams[] = "output_streams";
static constexpr char kJsonKeyMix[] = "mix";
static constexpr char kJsonKeyLinearize[] = "linearize";

rapidjson::SchemaDocument LoadProcessConfigSchema() {
  rapidjson::Document schema_doc;
  const rapidjson::ParseResult result = schema_doc.Parse(kAudioCoreConfigSchema);
  FXL_CHECK(!result.IsError()) << rapidjson::GetParseError_En(result.Code()) << "("
                               << result.Offset() << ")";
  return rapidjson::SchemaDocument(schema_doc);
}

fit::result<VolumeCurve, VolumeCurve::Error> ParseVolumeCurveFromJsonObject(
    const rapidjson::Value& value) {
  FXL_CHECK(value.IsArray());
  std::vector<VolumeCurve::VolumeMapping> mappings;
  for (const auto& mapping : value.GetArray()) {
    mappings.emplace_back(mapping["level"].GetFloat(), mapping["db"].GetFloat());
  }

  return VolumeCurve::FromMappings(std::move(mappings));
}

fuchsia::media::AudioRenderUsage UsageFromString(std::string_view string) {
  if (string == "media") {
    return fuchsia::media::AudioRenderUsage::MEDIA;
  } else if (string == "background") {
    return fuchsia::media::AudioRenderUsage::BACKGROUND;
  } else if (string == "communications") {
    return fuchsia::media::AudioRenderUsage::COMMUNICATION;
  } else if (string == "interruption") {
    return fuchsia::media::AudioRenderUsage::INTERRUPTION;
  } else if (string == "system_agent") {
    return fuchsia::media::AudioRenderUsage::SYSTEM_AGENT;
  }
  FXL_CHECK(false);
  return fuchsia::media::AudioRenderUsage::MEDIA;
}

PipelineConfig::Effect ParseEffectFromJsonObject(const rapidjson::Value& value) {
  FXL_CHECK(value.IsObject());
  PipelineConfig::Effect effect;

  auto it = value.FindMember(kJsonKeyLib);
  FXL_CHECK(it != value.MemberEnd() && it->value.IsString());
  effect.lib_name = it->value.GetString();

  it = value.FindMember(kJsonKeyName);
  if (it != value.MemberEnd()) {
    FXL_CHECK(it->value.IsString());
    effect.effect_name = it->value.GetString();
  }

  it = value.FindMember(kJsonKeyConfig);
  if (it != value.MemberEnd()) {
    rapidjson::StringBuffer config_buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(config_buf);
    it->value.Accept(writer);
    effect.effect_config = config_buf.GetString();
  }
  return effect;
}

PipelineConfig::MixGroup ParseMixGroupFromJsonObject(const rapidjson::Value& value) {
  FXL_CHECK(value.IsObject());
  PipelineConfig::MixGroup mix_group;

  auto it = value.FindMember(kJsonKeyName);
  if (it != value.MemberEnd()) {
    FXL_CHECK(it->value.IsString());
    mix_group.name = it->value.GetString();
  }

  it = value.FindMember(kJsonKeyStreams);
  if (it != value.MemberEnd()) {
    FXL_CHECK(it->value.IsArray());
    for (const auto& stream_type : it->value.GetArray()) {
      FXL_CHECK(stream_type.IsString());
      mix_group.input_streams.push_back(UsageFromString(stream_type.GetString()));
    }
  }

  it = value.FindMember(kJsonKeyEffects);
  if (it != value.MemberEnd()) {
    FXL_CHECK(it->value.IsArray());
    for (const auto& effect : it->value.GetArray()) {
      mix_group.effects.push_back(ParseEffectFromJsonObject(effect));
    }
  }
  return mix_group;
}

void ParsePipelineConfigFromJsonObject(const rapidjson::Value& value,
                                       ProcessConfig::Builder* config_builder) {
  auto it = value.FindMember(kJsonKeyOutputStreams);
  if (it != value.MemberEnd()) {
    FXL_CHECK(it->value.IsArray());
    for (const auto& group : it->value.GetArray()) {
      config_builder->AddOutputStreamEffects(ParseMixGroupFromJsonObject(group));
    }
  }

  it = value.FindMember(kJsonKeyMix);
  if (it != value.MemberEnd()) {
    config_builder->SetMixEffects(ParseMixGroupFromJsonObject(it->value));
  }

  it = value.FindMember(kJsonKeyLinearize);
  if (it != value.MemberEnd()) {
    config_builder->SetLinearizeEffects(ParseMixGroupFromJsonObject(it->value));
  }
}

}  // namespace

std::optional<ProcessConfig> ProcessConfigLoader::LoadProcessConfig(const char* filename) {
  std::string buffer;
  const auto file_exists = files::ReadFileToString(filename, &buffer);
  if (!file_exists) {
    return std::nullopt;
  }

  rapidjson::Document doc;
  const rapidjson::ParseResult parse_res = doc.ParseInsitu(buffer.data());
  if (parse_res.IsError()) {
    FXL_LOG(FATAL) << "Parse error (" << rapidjson::GetParseError_En(parse_res.Code())
                   << ") when reading " << filename << ":" << parse_res.Offset();
  }

  const auto schema = LoadProcessConfigSchema();
  rapidjson::SchemaValidator validator(schema);
  if (!doc.Accept(validator)) {
    rapidjson::StringBuffer error_buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(error_buf);
    validator.GetError().Accept(writer);
    FXL_LOG(FATAL) << "Schema validation error (" << error_buf.GetString() << ") when reading "
                   << filename;
  }

  auto curve_result = ParseVolumeCurveFromJsonObject(doc[kJsonKeyVolumeCurve]);
  if (!curve_result.is_ok()) {
    FXL_LOG(FATAL) << "Invalid volume curve; error: " << curve_result.take_error();
  }

  auto config_builder = ProcessConfig::Builder();
  config_builder.SetDefaultVolumeCurve(curve_result.take_value());

  // Add in audio effects if any are present.
  auto it = doc.FindMember(kJsonKeyPipeline);
  if (it != doc.MemberEnd()) {
    ParsePipelineConfigFromJsonObject(it->value, &config_builder);
  }

  return {config_builder.Build()};
}

}  // namespace media::audio
