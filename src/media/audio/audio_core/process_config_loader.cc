// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <fuchsia/media/audio/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <optional>
#include <sstream>
#include <string_view>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>

#include "rapidjson/prettywriter.h"
#include "src/lib/files/file.h"
#include "src/media/audio/audio_core/mix_profile_config.h"

#include "src/media/audio/audio_core/schema/audio_core_config_schema.inl"

namespace media::audio {

namespace {

constexpr char kJsonKeyMixProfile[] = "mix_profile";
constexpr char kJsonKeyMixProfileCapacityUsec[] = "capacity_usec";
constexpr char kJsonKeyMixProfileDeadlineUsec[] = "deadline_usec";
constexpr char kJsonKeyMixProfilePeriodUsec[] = "period_usec";
constexpr char kJsonKeyVolumeCurve[] = "volume_curve";
constexpr char kJsonKeyPipeline[] = "pipeline";
constexpr char kJsonKeyLib[] = "lib";
constexpr char kJsonKeyName[] = "name";
constexpr char kJsonKeyRate[] = "rate";
constexpr char kJsonKeyEffect[] = "effect";
constexpr char kJsonKeyConfig[] = "config";
constexpr char kJsonKeyStreams[] = "streams";
constexpr char kJsonKeyInputs[] = "inputs";
constexpr char kJsonKeyEffects[] = "effects";
constexpr char kJsonKeyEffectOverFidl[] = "effect_over_fidl";
constexpr char kJsonKeyLoopback[] = "loopback";
constexpr char kJsonKeyDeviceId[] = "device_id";
constexpr char kJsonKeyOutputRate[] = "output_rate";
constexpr char kJsonKeyOutputChannels[] = "output_channels";
constexpr char kJsonKeyInputDevices[] = "input_devices";
constexpr char kJsonKeyOutputDevices[] = "output_devices";
constexpr char kJsonKeySupportedStreamTypes[] = "supported_stream_types";
constexpr char kJsonKeySupportedOutputStreamTypes[] = "supported_output_stream_types";
constexpr char kJsonKeyEligibleForLoopback[] = "eligible_for_loopback";
constexpr char kJsonKeyIndependentVolumeControl[] = "independent_volume_control";
constexpr char kJsonKeyDriverGainDb[] = "driver_gain_db";
constexpr char kJsonKeySoftwareGainDb[] = "software_gain_db";
constexpr char kJsonKeyMinGainDb[] = "min_gain_db";
constexpr char kJsonKeyMaxGainDb[] = "max_gain_db";
constexpr char kJsonKeyThermalStates[] = "thermal_states";
constexpr char kJsonKeyThermalStateNumber[] = "state_number";
constexpr char kJsonKeyThermalEffectConfigs[] = "effect_configs";

void CountLoopbackStages(const PipelineConfig::MixGroup& mix_group, uint32_t* count) {
  if (mix_group.loopback) {
    ++*count;
  }
  for (const auto& input : mix_group.inputs) {
    CountLoopbackStages(input, count);
  }
}

uint32_t CountLoopbackStages(const PipelineConfig::MixGroup& root) {
  uint32_t count = 0;
  CountLoopbackStages(root, &count);
  return count;
}

fpromise::result<rapidjson::SchemaDocument, std::string> LoadProcessConfigSchema() {
  rapidjson::Document schema_doc;
  const rapidjson::ParseResult result =
      schema_doc.Parse<rapidjson::kParseIterativeFlag>(kAudioCoreConfigSchema);
  if (result.IsError()) {
    std::ostringstream oss;
    oss << "Failed to load config schema: " << rapidjson::GetParseError_En(result.Code()) << "("
        << result.Offset() << ")";
    return fpromise::error(oss.str());
  }
  return fpromise::ok(rapidjson::SchemaDocument(schema_doc));
}

MixProfileConfig ParseMixProfileFromJsonObject(const rapidjson::Value& value) {
  FX_CHECK(value.IsObject());
  MixProfileConfig mix_profile_config;
  if (const auto it = value.FindMember(kJsonKeyMixProfileCapacityUsec); it != value.MemberEnd()) {
    FX_CHECK(it->value.IsUint());
    mix_profile_config.capacity = zx::usec(it->value.GetUint());
  }
  if (const auto it = value.FindMember(kJsonKeyMixProfileDeadlineUsec); it != value.MemberEnd()) {
    FX_CHECK(it->value.IsUint());
    mix_profile_config.deadline = zx::usec(it->value.GetUint());
  }
  if (const auto it = value.FindMember(kJsonKeyMixProfilePeriodUsec); it != value.MemberEnd()) {
    FX_CHECK(it->value.IsUint());
    mix_profile_config.period = zx::usec(it->value.GetUint());
  }
  return mix_profile_config;
}

fpromise::result<VolumeCurve, std::string> ParseVolumeCurveFromJsonObject(
    const rapidjson::Value& value) {
  FX_CHECK(value.IsArray());
  std::vector<VolumeCurve::VolumeMapping> mappings;
  for (const auto& mapping : value.GetArray()) {
    if (mapping["db"].IsString()) {
      std::string str = mapping["db"].GetString();
      if (str != "MUTED") {
        return fpromise::error("unknown gain value '" + str + "'");
      }
      mappings.emplace_back(mapping["level"].GetFloat(), fuchsia::media::audio::MUTED_GAIN_DB);
    } else {
      mappings.emplace_back(mapping["level"].GetFloat(), mapping["db"].GetFloat());
    }
  }

  return VolumeCurve::FromMappings(std::move(mappings));
}

std::optional<RenderUsage> RenderUsageFromString(std::string_view string) {
  if (string == "media" || string == "render:media") {
    return RenderUsage::MEDIA;
  } else if (string == "background" || string == "render:background") {
    return RenderUsage::BACKGROUND;
  } else if (string == "communications" || string == "render:communications") {
    return RenderUsage::COMMUNICATION;
  } else if (string == "interruption" || string == "render:interruption") {
    return RenderUsage::INTERRUPTION;
  } else if (string == "system_agent" || string == "render:system_agent") {
    return RenderUsage::SYSTEM_AGENT;
  } else if (string == "ultrasound" || string == "render:ultrasound") {
    return RenderUsage::ULTRASOUND;
  }
  return std::nullopt;
}

std::optional<CaptureUsage> CaptureUsageFromString(std::string_view string) {
  if (string == "background" || string == "capture:background") {
    return CaptureUsage::BACKGROUND;
  } else if (string == "foreground" || string == "capture:foreground") {
    return CaptureUsage::FOREGROUND;
  } else if (string == "system_agent" || string == "capture:system_agent") {
    return CaptureUsage::SYSTEM_AGENT;
  } else if (string == "communications" || string == "capture:communications") {
    return CaptureUsage::COMMUNICATION;
  } else if (string == "ultrasound" || string == "capture:ultrasound") {
    return CaptureUsage::ULTRASOUND;
  } else if (string == "loopback" || string == "capture:loopback") {
    return CaptureUsage::LOOPBACK;
  }
  return std::nullopt;
}

std::optional<StreamUsage> StreamUsageFromString(std::string_view string) {
  auto render_usage = RenderUsageFromString(string);
  if (render_usage) {
    return StreamUsage::WithRenderUsage(*render_usage);
  }
  auto capture_usage = CaptureUsageFromString(string);
  if (capture_usage) {
    return StreamUsage::WithCaptureUsage(*capture_usage);
  }
  return std::nullopt;
}

PipelineConfig::EffectV1 ParseEffectV1FromJsonObject(const rapidjson::Value& value) {
  FX_CHECK(value.IsObject());
  PipelineConfig::EffectV1 effect;

  auto it = value.FindMember(kJsonKeyLib);
  FX_CHECK(it != value.MemberEnd() && it->value.IsString());
  effect.lib_name = it->value.GetString();

  it = value.FindMember(kJsonKeyEffect);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsString());
    effect.effect_name = it->value.GetString();
  }

  it = value.FindMember(kJsonKeyName);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsString());
    effect.instance_name = it->value.GetString();
  }

  it = value.FindMember(kJsonKeyConfig);
  if (it != value.MemberEnd()) {
    rapidjson::StringBuffer config_buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(config_buf);
    it->value.Accept(writer);
    effect.effect_config = config_buf.GetString();
  }

  it = value.FindMember(kJsonKeyOutputChannels);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsUint());
    effect.output_channels = it->value.GetUint();
  }
  return effect;
}

PipelineConfig::EffectV2 ParseEffectV2FromJsonObject(const rapidjson::Value& value) {
  FX_CHECK(value.IsObject());
  PipelineConfig::EffectV2 effect;

  auto it = value.FindMember(kJsonKeyName);
  FX_CHECK(it != value.MemberEnd() && it->value.IsString());
  effect.instance_name = it->value.GetString();

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
      auto render_usage = RenderUsageFromString(stream_type.GetString());
      FX_CHECK(render_usage);
      mix_group.input_streams.push_back(*render_usage);
    }
  }

  it = value.FindMember(kJsonKeyEffects);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsArray());
    for (const auto& effect : it->value.GetArray()) {
      mix_group.effects_v1.push_back(ParseEffectV1FromJsonObject(effect));
    }
  }

  it = value.FindMember(kJsonKeyEffectOverFidl);
  if (it != value.MemberEnd()) {
    mix_group.effects_v2 = ParseEffectV2FromJsonObject(it->value);
  }

  it = value.FindMember(kJsonKeyInputs);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsArray());
    for (const auto& input : it->value.GetArray()) {
      mix_group.inputs.push_back(ParseMixGroupFromJsonObject(input));
    }
  }

  it = value.FindMember(kJsonKeyLoopback);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsBool());
    mix_group.loopback = it->value.GetBool();
  } else {
    mix_group.loopback = false;
  }

  it = value.FindMember(kJsonKeyOutputRate);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsUint());
    mix_group.output_rate = it->value.GetUint();
  } else {
    mix_group.output_rate = PipelineConfig::kDefaultMixGroupRate;
  }

  it = value.FindMember(kJsonKeyOutputChannels);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsUint());
    mix_group.output_channels = it->value.GetUint();
  } else {
    mix_group.output_channels = PipelineConfig::kDefaultMixGroupChannels;
  }

  it = value.FindMember(kJsonKeyMinGainDb);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsNumber());
    FX_CHECK(it->value.GetFloat() >= fuchsia::media::audio::MUTED_GAIN_DB);
    for (auto usage : mix_group.input_streams) {
      FX_CHECK(usage != RenderUsage::ULTRASOUND) << "cannot set gain limits for ultrasound";
    }
    mix_group.min_gain_db = it->value.GetFloat();
  }

  it = value.FindMember(kJsonKeyMaxGainDb);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsNumber());
    FX_CHECK(it->value.GetFloat() <= fuchsia::media::audio::MAX_GAIN_DB);
    for (auto usage : mix_group.input_streams) {
      FX_CHECK(usage != RenderUsage::ULTRASOUND) << "cannot set gain limits for ultrasound";
    }
    mix_group.max_gain_db = it->value.GetFloat();
  }

  return mix_group;
}

std::optional<audio_stream_unique_id_t> ParseDeviceIdFromJsonString(const rapidjson::Value& value) {
  FX_CHECK(value.IsString());
  const auto* device_id_string = value.GetString();

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
  return device_id;
}

// Returns Some(vector) if there is a list of concrete device id's. Returns nullopt for the default
// configuration.
std::optional<std::vector<audio_stream_unique_id_t>> ParseDeviceIdFromJsonValue(
    const rapidjson::Value& value) {
  std::vector<audio_stream_unique_id_t> result;
  if (value.IsString()) {
    auto device_id = ParseDeviceIdFromJsonString(value);
    if (device_id) {
      result.push_back(*device_id);
    } else {
      return std::nullopt;
    }
  } else if (value.IsArray()) {
    for (const auto& device_id_value : value.GetArray()) {
      auto device_id = ParseDeviceIdFromJsonString(device_id_value);
      if (device_id) {
        result.push_back(*device_id);
      } else {
        return std::nullopt;
      }
    }
  }
  return {result};
}

StreamUsageSet ParseStreamUsageSetFromJsonArray(const rapidjson::Value& value,
                                                StreamUsageSet* all_supported_usages = nullptr) {
  FX_CHECK(value.IsArray());
  StreamUsageSet supported_stream_types;
  for (const auto& stream_type : value.GetArray()) {
    FX_CHECK(stream_type.IsString());
    const auto supported_usage = StreamUsageFromString(stream_type.GetString());
    FX_CHECK(supported_usage);
    if (all_supported_usages) {
      all_supported_usages->insert(*supported_usage);
    }
    supported_stream_types.insert(*supported_usage);
  }
  return supported_stream_types;
}

fpromise::result<std::pair<std::optional<std::vector<audio_stream_unique_id_t>>,
                           DeviceConfig::OutputDeviceProfile>,
                 std::string>
ParseOutputDeviceProfileFromJsonObject(const rapidjson::Value& value,
                                       StreamUsageSet* all_supported_usages,
                                       VolumeCurve volume_curve) {
  FX_CHECK(value.IsObject());

  auto device_id_it = value.FindMember(kJsonKeyDeviceId);
  FX_CHECK(device_id_it != value.MemberEnd());

  auto device_id = ParseDeviceIdFromJsonValue(device_id_it->value);

  bool eligible_for_loopback = false;
  auto eligible_for_loopback_it = value.FindMember(kJsonKeyEligibleForLoopback);
  if (eligible_for_loopback_it != value.MemberEnd()) {
    FX_CHECK(eligible_for_loopback_it->value.IsBool());
    eligible_for_loopback = eligible_for_loopback_it->value.GetBool();
  }

  auto independent_volume_control_it = value.FindMember(kJsonKeyIndependentVolumeControl);
  bool independent_volume_control = false;
  if (independent_volume_control_it != value.MemberEnd()) {
    FX_CHECK(independent_volume_control_it->value.IsBool());
    independent_volume_control = independent_volume_control_it->value.GetBool();
  }

  float driver_gain_db = 0.0;
  auto driver_gain_db_it = value.FindMember(kJsonKeyDriverGainDb);
  if (driver_gain_db_it != value.MemberEnd()) {
    FX_CHECK(driver_gain_db_it->value.IsNumber());
    driver_gain_db = driver_gain_db_it->value.GetDouble();
  }

  // software_gain_db is not supported due to implementation challenges.
  // See discussion on fxrev.dev/641221.

  StreamUsageSet supported_stream_types;
  auto supported_stream_types_it = value.FindMember(kJsonKeySupportedOutputStreamTypes);
  if (supported_stream_types_it != value.MemberEnd()) {
    supported_stream_types =
        ParseStreamUsageSetFromJsonArray(supported_stream_types_it->value, all_supported_usages);
  } else {
    supported_stream_types_it = value.FindMember(kJsonKeySupportedStreamTypes);
    if (supported_stream_types_it != value.MemberEnd()) {
      supported_stream_types =
          ParseStreamUsageSetFromJsonArray(supported_stream_types_it->value, all_supported_usages);
    } else {
      FX_CHECK(false) << "Missing required stream usage set";
    }
  }
  auto& supported_stream_types_value = supported_stream_types_it->value;
  FX_CHECK(supported_stream_types_value.IsArray());

  bool supports_loopback =
      supported_stream_types.find(StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)) !=
      supported_stream_types.end();
  auto pipeline_it = value.FindMember(kJsonKeyPipeline);
  PipelineConfig pipeline_config;
  if (pipeline_it != value.MemberEnd()) {
    FX_CHECK(pipeline_it->value.IsObject());
    auto root = ParseMixGroupFromJsonObject(pipeline_it->value);
    auto loopback_stages = CountLoopbackStages(root);
    if (supports_loopback) {
      if (loopback_stages > 1) {
        return fpromise::error("More than 1 loopback stage specified");
      }
      if (loopback_stages == 0) {
        return fpromise::error("Device supports loopback but no loopback point specified");
      }
    }
    pipeline_config = PipelineConfig(std::move(root));
  } else {
    // If no pipeline is specified, we'll use a single mix stage.
    pipeline_config.mutable_root().name = "default";
    pipeline_config.mutable_root().loopback = supports_loopback;
    for (const auto& stream_usage : supported_stream_types) {
      if (stream_usage.is_render_usage()) {
        pipeline_config.mutable_root().input_streams.emplace_back(stream_usage.render_usage());
      }
    }
  }

  return fpromise::ok(std::make_pair(
      device_id,
      DeviceConfig::OutputDeviceProfile(eligible_for_loopback, std::move(supported_stream_types),
                                        std::move(volume_curve), independent_volume_control,
                                        std::move(pipeline_config), driver_gain_db,
                                        /*software_gain_db=*/0)));
}

fpromise::result<void, std::string> ParseOutputDevicePoliciesFromJsonObject(
    const rapidjson::Value& output_device_profiles, ProcessConfigBuilder* config_builder) {
  FX_CHECK(output_device_profiles.IsArray());

  StreamUsageSet all_supported_usages;
  for (const auto& output_device_profile : output_device_profiles.GetArray()) {
    auto result = ParseOutputDeviceProfileFromJsonObject(
        output_device_profile, &all_supported_usages, config_builder->default_volume_curve());
    if (result.is_error()) {
      return result.take_error_result();
    }
    config_builder->AddDeviceProfile(result.take_value());
  }

  // We expect all the usages that clients can select are supported.
  for (const auto& render_usage : kFidlRenderUsages) {
    auto stream_usage = StreamUsage::WithRenderUsage(render_usage);
    if (all_supported_usages.find(stream_usage) == all_supported_usages.end()) {
      std::ostringstream oss;
      oss << "No output to support usage " << stream_usage.ToString();
      return fpromise::error(oss.str());
    }
  }
  return fpromise::ok();
}

fpromise::result<std::pair<std::optional<std::vector<audio_stream_unique_id_t>>,
                           DeviceConfig::InputDeviceProfile>>
ParseInputDeviceProfileFromJsonObject(const rapidjson::Value& value, VolumeCurve volume_curve) {
  FX_CHECK(value.IsObject());

  auto device_id_it = value.FindMember(kJsonKeyDeviceId);
  FX_CHECK(device_id_it != value.MemberEnd());

  auto device_id = ParseDeviceIdFromJsonValue(device_id_it->value);

  auto rate_it = value.FindMember(kJsonKeyRate);
  FX_CHECK(rate_it != value.MemberEnd());
  if (!rate_it->value.IsUint()) {
    return fpromise::error();
  }
  auto rate = rate_it->value.GetUint();

  float driver_gain_db = 0.0;
  auto driver_gain_db_it = value.FindMember(kJsonKeyDriverGainDb);
  if (driver_gain_db_it != value.MemberEnd()) {
    FX_CHECK(driver_gain_db_it->value.IsNumber());
    driver_gain_db = driver_gain_db_it->value.GetDouble();
  }

  float software_gain_db = 0.0;
  auto software_gain_db_it = value.FindMember(kJsonKeySoftwareGainDb);
  if (software_gain_db_it != value.MemberEnd()) {
    FX_CHECK(software_gain_db_it->value.IsNumber());
    software_gain_db = software_gain_db_it->value.GetDouble();
  }

  StreamUsageSet supported_stream_types;
  auto supported_stream_types_it = value.FindMember(kJsonKeySupportedStreamTypes);
  if (supported_stream_types_it != value.MemberEnd()) {
    supported_stream_types = ParseStreamUsageSetFromJsonArray(supported_stream_types_it->value);
    return fpromise::ok(std::make_pair(
        device_id, DeviceConfig::InputDeviceProfile(rate, std::move(supported_stream_types),
                                                    std::move(volume_curve), driver_gain_db,
                                                    software_gain_db)));
  }

  return fpromise::ok(std::make_pair(
      device_id, DeviceConfig::InputDeviceProfile(rate, driver_gain_db, software_gain_db)));
}

fpromise::result<> ParseInputDevicePoliciesFromJsonObject(
    const rapidjson::Value& input_device_profiles, ProcessConfigBuilder* config_builder) {
  FX_CHECK(input_device_profiles.IsArray());

  for (const auto& input_device_profile : input_device_profiles.GetArray()) {
    auto result = ParseInputDeviceProfileFromJsonObject(input_device_profile,
                                                        config_builder->default_volume_curve());
    if (result.is_error()) {
      return fpromise::error();
    }
    config_builder->AddDeviceProfile(result.take_value());
  }
  return fpromise::ok();
}

fpromise::result<std::vector<ThermalConfig::EffectConfig>, std::string>
ParseEffectConfigsFromJsonObject(const rapidjson::Value& json_entry) {
  std::vector<ThermalConfig::EffectConfig> effect_configs;

  if (!json_entry.IsObject()) {
    return fpromise::error("Parse error: effect_config entry value must be an object");
  }
  const auto effect_configs_object = json_entry.GetObject();
  if (effect_configs_object.ObjectEmpty()) {
    return fpromise::error("Parse error: effect_config object must not be empty");
  }

  for (const auto& effect_config_entry : effect_configs_object) {
    if (!effect_config_entry.name.IsString()) {
      return fpromise::error("Parse error: effect_config key must be a string");
    }
    const auto effect_config_name = effect_config_entry.name.GetString();

    if (!effect_config_entry.value.IsObject()) {
      return fpromise::error("Parse error: effect_config value must be an object");
    }
    rapidjson::StringBuffer config_buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(config_buffer);
    effect_config_entry.value.Accept(writer);
    const auto config_string = config_buffer.GetString();

    effect_configs.emplace_back(effect_config_name, config_string);
  }
  return fpromise::ok(effect_configs);
}

fpromise::result<ThermalConfig::State, std::string> ParseThermalStateFromJsonObject(
    const rapidjson::Value& json_entry) {
  if (!json_entry.IsObject()) {
    return fpromise::error("Parse error: thermal state entry must be an object");
  }

  // validate state_number
  auto thermal_state_number_key = json_entry.FindMember(kJsonKeyThermalStateNumber);
  if (thermal_state_number_key == json_entry.MemberEnd()) {
    return fpromise::error("Parse error: thermal state entry must contain state_number key");
  }
  if (!thermal_state_number_key->value.IsUint64()) {
    return fpromise::error("Parse error: thermal state_number key must be uint64");
  }
  const auto state_number = thermal_state_number_key->value.GetUint64();

  // get effect_configs dictionary
  auto effect_configs_key = json_entry.FindMember(kJsonKeyThermalEffectConfigs);
  if (effect_configs_key == json_entry.MemberEnd()) {
    return fpromise::error("Parse error: thermal state entry must contain effect_configs key");
  }

  auto result = ParseEffectConfigsFromJsonObject(effect_configs_key->value);
  if (result.is_error()) {
    return fpromise::error(result.error());
  }

  // create ThermalConfig::State
  return fpromise::ok(ThermalConfig::State{state_number, result.value()});
}

fpromise::result<void, std::string> ParseThermalStatesFromJsonObject(
    const rapidjson::Value& json_entry, ProcessConfigBuilder* config_builder) {
  if (!json_entry.IsArray()) {
    return fpromise::error("Parse error: thermal_states must be an array");
  }
  const auto& json_entries = json_entry.GetArray();
  if (json_entries.Empty()) {
    return fpromise::error("Parse error: thermal_states array cannot be empty");
  }

  // As we parse each thermal state, ensure that state[0] is specified, and that every thermal
  // state configures an identical set of effects.
  bool state0_found = false;
  std::optional<uint64_t> first_state_number;
  std::unordered_set<std::string> effect_config_names;

  for (const auto& json_state : json_entry.GetArray()) {
    auto result = ParseThermalStateFromJsonObject(json_state);
    if (result.is_error()) {
      return fpromise::error(result.error());
    }
    auto thermal_state_number = result.value().thermal_state_number();
    if (thermal_state_number == 0) {
      state0_found = true;
    }
    auto configs = result.value().effect_configs();
    if (!first_state_number.has_value()) {
      first_state_number = thermal_state_number;
      for (const auto& config : configs) {
        effect_config_names.insert(config.name());
      }
    } else {
      std::ostringstream error_str;
      if (configs.size() != effect_config_names.size()) {
        error_str << "First thermal state [" << *first_state_number << "] contains "
                  << effect_config_names.size() << " effect configs, but state ["
                  << thermal_state_number << "] contains " << configs.size() << " effect configs";
        return fpromise::error(error_str.str());
      }
      for (const auto& config : configs) {
        if (effect_config_names.find(config.name()) == effect_config_names.end()) {
          error_str << "Thermal state [" << thermal_state_number << "] contains effect config '"
                    << config.name() << "' which is not specified in state [" << *first_state_number
                    << "]";
          return fpromise::error(error_str.str());
        }
      }
    }
    config_builder->AddThermalConfigState(result.take_value());
  }

  if (!state0_found) {
    return fpromise::error("Thermal configuration must contain an entry for state 0");
  }

  return fpromise::ok();
}

}  // namespace

fpromise::result<ProcessConfig, std::string> ProcessConfigLoader::LoadProcessConfig(
    const char* filename) {
  std::string buffer;
  const auto file_exists = files::ReadFileToString(filename, &buffer);
  if (!file_exists) {
    return fpromise::error("File does not exist");
  }

  auto result = ParseProcessConfig(buffer);
  if (result.is_error()) {
    std::ostringstream oss;
    oss << "Parse error: " << result.error();
    return fpromise::error(oss.str());
  }

  return fpromise::ok(result.take_value());
}

fpromise::result<ProcessConfig, std::string> ProcessConfigLoader::ParseProcessConfig(
    const std::string& config) {
  rapidjson::Document doc;
  std::string parse_buffer = config;
  const rapidjson::ParseResult parse_res = doc.ParseInsitu(parse_buffer.data());
  if (parse_res.IsError()) {
    std::stringstream error;
    error << "Parse error (" << rapidjson::GetParseError_En(parse_res.Code())
          << "): " << parse_res.Offset();
    return fpromise::error(error.str());
  }

  auto schema_result = LoadProcessConfigSchema();
  if (schema_result.is_error()) {
    return fpromise::error(schema_result.take_error());
  }
  rapidjson::SchemaValidator validator(schema_result.value());
  if (!doc.Accept(validator)) {
    rapidjson::StringBuffer error_buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(error_buf);
    validator.GetError().Accept(writer);
    std::stringstream error;
    error << "Schema validation error (" << error_buf.GetString() << ")";
    return fpromise::error(error.str());
  }

  auto curve_result = ParseVolumeCurveFromJsonObject(doc[kJsonKeyVolumeCurve]);
  if (!curve_result.is_ok()) {
    std::stringstream error;
    error << "Invalid volume curve; error: " << curve_result.take_error();
    return fpromise::error(error.str());
  }

  auto config_builder = ProcessConfig::Builder();
  config_builder.SetDefaultVolumeCurve(curve_result.take_value());

  auto mix_profile_it = doc.FindMember(kJsonKeyMixProfile);
  if (mix_profile_it != doc.MemberEnd()) {
    config_builder.SetMixProfile(ParseMixProfileFromJsonObject(mix_profile_it->value));
  }

  auto output_devices_it = doc.FindMember(kJsonKeyOutputDevices);
  if (output_devices_it != doc.MemberEnd()) {
    auto result =
        ParseOutputDevicePoliciesFromJsonObject(output_devices_it->value, &config_builder);
    if (result.is_error()) {
      std::ostringstream oss;
      oss << "Failed to parse output device policies: " << result.error();
      return fpromise::error(oss.str());
    }
  }
  auto input_devices_it = doc.FindMember(kJsonKeyInputDevices);
  if (input_devices_it != doc.MemberEnd()) {
    auto result = ParseInputDevicePoliciesFromJsonObject(input_devices_it->value, &config_builder);
    if (result.is_error()) {
      return fpromise::error("Failed to parse input device policies");
    }
  }

  auto thermal_states_it = doc.FindMember(kJsonKeyThermalStates);
  if (thermal_states_it != doc.MemberEnd()) {
    auto result = ParseThermalStatesFromJsonObject(thermal_states_it->value, &config_builder);
    if (result.is_error()) {
      return fpromise::error(std::string("Failed to parse thermal state policies: ") +
                             result.take_error());
    }
  } else {
    FX_LOGS(INFO) << "This configuration contains no thermal state specification";
  }

  return fpromise::ok(config_builder.Build());
}

}  // namespace media::audio
