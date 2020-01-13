// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <sstream>
#include <string_view>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>

#include "rapidjson/prettywriter.h"
#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"

#include "src/media/audio/audio_core/schema/audio_core_config_schema.inl"

namespace media::audio {

namespace {

static constexpr char kJsonKeyVolumeCurve[] = "volume_curve";
static constexpr char kJsonKeyPipeline[] = "pipeline";
static constexpr char kJsonKeyLib[] = "lib";
static constexpr char kJsonKeyName[] = "name";
static constexpr char kJsonKeyConfig[] = "config";
static constexpr char kJsonKeyStreams[] = "streams";
static constexpr char kJsonKeyInputs[] = "inputs";
static constexpr char kJsonKeyEffects[] = "effects";
static constexpr char kJsonKeyRoutingPolicy[] = "routing_policy";
static constexpr char kJsonKeyDeviceProfiles[] = "device_profiles";
static constexpr char kJsonKeyDeviceId[] = "device_id";
static constexpr char kJsonKeySupportedOutputStreamTypes[] = "supported_output_stream_types";
static constexpr char kJsonKeyEligibleForLoopback[] = "eligible_for_loopback";
static constexpr char kJsonKeyIndependentVolumeControl[] = "independent_volume_control";

rapidjson::SchemaDocument LoadProcessConfigSchema() {
  rapidjson::Document schema_doc;
  const rapidjson::ParseResult result = schema_doc.Parse(kAudioCoreConfigSchema);
  FX_CHECK(!result.IsError()) << rapidjson::GetParseError_En(result.Code()) << "("
                              << result.Offset() << ")";
  return rapidjson::SchemaDocument(schema_doc);
}

fit::result<VolumeCurve, VolumeCurve::Error> ParseVolumeCurveFromJsonObject(
    const rapidjson::Value& value) {
  FX_CHECK(value.IsArray());
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
  FX_CHECK(false);
  return fuchsia::media::AudioRenderUsage::MEDIA;
}

PipelineConfig::Effect ParseEffectFromJsonObject(const rapidjson::Value& value) {
  FX_CHECK(value.IsObject());
  PipelineConfig::Effect effect;

  auto it = value.FindMember(kJsonKeyLib);
  FX_CHECK(it != value.MemberEnd() && it->value.IsString());
  effect.lib_name = it->value.GetString();

  it = value.FindMember(kJsonKeyName);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsString());
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
  FX_CHECK(value.IsObject());
  PipelineConfig::MixGroup mix_group;

  auto it = value.FindMember(kJsonKeyName);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsString());
    mix_group.name = it->value.GetString();
  }

  it = value.FindMember(kJsonKeyStreams);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsArray());
    for (const auto& stream_type : it->value.GetArray()) {
      FX_CHECK(stream_type.IsString());
      mix_group.input_streams.push_back(UsageFromString(stream_type.GetString()));
    }
  }

  it = value.FindMember(kJsonKeyEffects);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsArray());
    for (const auto& effect : it->value.GetArray()) {
      mix_group.effects.push_back(ParseEffectFromJsonObject(effect));
    }
  }

  it = value.FindMember(kJsonKeyInputs);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsArray());
    for (const auto& input : it->value.GetArray()) {
      mix_group.inputs.push_back(ParseMixGroupFromJsonObject(input));
    }
  }
  return mix_group;
}

std::pair<std::optional<audio_stream_unique_id_t>, RoutingConfig::DeviceProfile>
ParseDeviceRoutingProfileFromJsonObject(const rapidjson::Value& value,
                                        std::unordered_set<uint32_t>* all_supported_usages) {
  FX_CHECK(value.IsObject());

  auto device_id_it = value.FindMember(kJsonKeyDeviceId);
  FX_CHECK(device_id_it != value.MemberEnd());
  auto& device_id_value = device_id_it->value;
  FX_CHECK(device_id_value.IsString());
  const auto* device_id_string = device_id_value.GetString();

  std::optional<audio_stream_unique_id_t> device_id;
  if (strcmp(device_id_string, "*") != 0) {
    device_id = {{}};
    const auto captures = std::sscanf(
        device_id_string,
        "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8
        "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8
        "%02" SCNx8 "%02" SCNx8,
        &device_id->data[0], &device_id->data[1], &device_id->data[2], &device_id->data[3],
        &device_id->data[4], &device_id->data[5], &device_id->data[6], &device_id->data[7],
        &device_id->data[8], &device_id->data[9], &device_id->data[10], &device_id->data[11],
        &device_id->data[12], &device_id->data[13], &device_id->data[14], &device_id->data[15]);
    FX_CHECK(captures == 16);
  }

  auto eligible_for_loopback_it = value.FindMember(kJsonKeyEligibleForLoopback);
  FX_CHECK(eligible_for_loopback_it != value.MemberEnd());
  FX_CHECK(eligible_for_loopback_it->value.IsBool());
  const auto eligible_for_loopback = eligible_for_loopback_it->value.GetBool();

  auto independent_volume_control_it = value.FindMember(kJsonKeyIndependentVolumeControl);
  bool independent_volume_control = false;
  if (independent_volume_control_it != value.MemberEnd()) {
    FX_CHECK(independent_volume_control_it->value.IsBool());
    independent_volume_control = independent_volume_control_it->value.GetBool();
  }

  auto supported_output_stream_types_it = value.FindMember(kJsonKeySupportedOutputStreamTypes);
  FX_CHECK(supported_output_stream_types_it != value.MemberEnd());
  auto& supported_output_stream_types_value = supported_output_stream_types_it->value;
  FX_CHECK(supported_output_stream_types_value.IsArray());

  RoutingConfig::UsageSupportSet supported_output_stream_types;
  for (const auto& stream_type : supported_output_stream_types_value.GetArray()) {
    FX_CHECK(stream_type.IsString());
    const auto supported_usage = fidl::ToUnderlying(UsageFromString(stream_type.GetString()));
    all_supported_usages->insert(supported_usage);
    supported_output_stream_types.insert(supported_usage);
  }

  auto pipeline_it = value.FindMember(kJsonKeyPipeline);
  PipelineConfig pipeline_config;
  if (pipeline_it != value.MemberEnd()) {
    FX_CHECK(pipeline_it->value.IsObject());
    pipeline_config = PipelineConfig(ParseMixGroupFromJsonObject(pipeline_it->value));
  } else {
    pipeline_config = PipelineConfig::Default();
  }
  return {device_id, RoutingConfig::DeviceProfile(
                         eligible_for_loopback, std::move(supported_output_stream_types),
                         independent_volume_control, std::move(pipeline_config))};
}

void ParseRoutingPolicyFromJsonObject(const rapidjson::Value& value,
                                      ProcessConfigBuilder* config_builder) {
  FX_CHECK(value.IsObject());

  auto device_profiles_it = value.FindMember(kJsonKeyDeviceProfiles);
  FX_CHECK(device_profiles_it != value.MemberEnd());
  auto& device_profiles = device_profiles_it->value;
  FX_CHECK(device_profiles.IsArray());

  std::unordered_set<uint32_t> all_supported_usages;
  for (const auto& device_profile : device_profiles.GetArray()) {
    config_builder->AddDeviceRoutingProfile(
        ParseDeviceRoutingProfileFromJsonObject(device_profile, &all_supported_usages));
  }

  FX_CHECK(all_supported_usages.size() == fuchsia::media::RENDER_USAGE_COUNT)
      << "Not all output usages are supported in the config";
}

}  // namespace

std::optional<ProcessConfig> ProcessConfigLoader::LoadProcessConfig(const char* filename) {
  std::string buffer;
  const auto file_exists = files::ReadFileToString(filename, &buffer);
  if (!file_exists) {
    return std::nullopt;
  }

  auto result = ParseProcessConfig(buffer);
  if (result.is_error()) {
    FX_LOGS(FATAL) << "Failed to parse " << filename << "; error: " << result.error();
  }

  return result.take_value();
}

fit::result<ProcessConfig, std::string> ProcessConfigLoader::ParseProcessConfig(
    const std::string& config) {
  rapidjson::Document doc;
  std::string parse_buffer = config;
  const rapidjson::ParseResult parse_res = doc.ParseInsitu(parse_buffer.data());
  if (parse_res.IsError()) {
    std::stringstream error;
    error << "Parse error (" << rapidjson::GetParseError_En(parse_res.Code())
          << "): " << parse_res.Offset();
    return fit::error(error.str());
  }

  const auto schema = LoadProcessConfigSchema();
  rapidjson::SchemaValidator validator(schema);
  if (!doc.Accept(validator)) {
    rapidjson::StringBuffer error_buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(error_buf);
    validator.GetError().Accept(writer);
    std::stringstream error;
    error << "Schema validation error (" << error_buf.GetString() << ")";
    return fit::error(error.str());
  }

  auto curve_result = ParseVolumeCurveFromJsonObject(doc[kJsonKeyVolumeCurve]);
  if (!curve_result.is_ok()) {
    std::stringstream error;
    error << "Invalid volume curve; error: " << curve_result.take_error();
    return fit::error(error.str());
  }

  auto config_builder = ProcessConfig::Builder();
  config_builder.SetDefaultVolumeCurve(curve_result.take_value());

  auto routing_policy_it = doc.FindMember(kJsonKeyRoutingPolicy);
  if (routing_policy_it != doc.MemberEnd()) {
    ParseRoutingPolicyFromJsonObject(routing_policy_it->value, &config_builder);
  }

  return fit::ok(config_builder.Build());
}

}  // namespace media::audio
