// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>
#include <string_view>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>

#include "rapidjson/prettywriter.h"
#include "src/lib/files/file.h"

#include "src/media/audio/audio_core/schema/audio_core_config_schema.inl"

namespace media::audio {

namespace {

static constexpr char kJsonKeyVolumeCurve[] = "volume_curve";
static constexpr char kJsonKeyPipeline[] = "pipeline";
static constexpr char kJsonKeyLib[] = "lib";
static constexpr char kJsonKeyName[] = "name";
static constexpr char kJsonKeyRate[] = "rate";
static constexpr char kJsonKeyEffect[] = "effect";
static constexpr char kJsonKeyConfig[] = "config";
static constexpr char kJsonKeyStreams[] = "streams";
static constexpr char kJsonKeyInputs[] = "inputs";
static constexpr char kJsonKeyEffects[] = "effects";
static constexpr char kJsonKeyLoopback[] = "loopback";
static constexpr char kJsonKeyDeviceId[] = "device_id";
static constexpr char kJsonKeyOutputRate[] = "output_rate";
static constexpr char kJsonKeyOutputChannels[] = "output_channels";
static constexpr char kJsonKeyInputDevices[] = "input_devices";
static constexpr char kJsonKeyOutputDevices[] = "output_devices";
static constexpr char kJsonKeySupportedStreamTypes[] = "supported_stream_types";
static constexpr char kJsonKeySupportedOutputStreamTypes[] = "supported_output_stream_types";
static constexpr char kJsonKeyEligibleForLoopback[] = "eligible_for_loopback";
static constexpr char kJsonKeyIndependentVolumeControl[] = "independent_volume_control";
static constexpr char kJsonKeyDriverGainDb[] = "driver_gain_db";
static constexpr char kJsonKeyThermalPolicy[] = "thermal_policy";
static constexpr char kJsonKeyTargetName[] = "target_name";
static constexpr char kJsonKeyStates[] = "states";
static constexpr char kJsonKeyTripPoint[] = "trip_point";
static constexpr char kJsonKeyTripPointDeactivateBelow[] = "deactivate_below";
static constexpr char kJsonKeyTripPointActivateAt[] = "activate_at";
static constexpr char kJsonKeyStateTransitions[] = "state_transitions";

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

fit::result<rapidjson::SchemaDocument, std::string> LoadProcessConfigSchema() {
  rapidjson::Document schema_doc;
  const rapidjson::ParseResult result = schema_doc.Parse(kAudioCoreConfigSchema);
  if (result.IsError()) {
    std::ostringstream oss;
    oss << "Failed to load config schema: " << rapidjson::GetParseError_En(result.Code()) << "("
        << result.Offset() << ")";
    return fit::error(oss.str());
  }
  return fit::ok(rapidjson::SchemaDocument(schema_doc));
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

PipelineConfig::Effect ParseEffectFromJsonObject(const rapidjson::Value& value) {
  FX_CHECK(value.IsObject());
  PipelineConfig::Effect effect;

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
    FX_DCHECK(it->value.IsUint());
    effect.output_channels = it->value.GetUint();
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
      auto render_usage = RenderUsageFromString(stream_type.GetString());
      FX_DCHECK(render_usage);
      mix_group.input_streams.push_back(*render_usage);
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

  it = value.FindMember(kJsonKeyLoopback);
  if (it != value.MemberEnd()) {
    FX_CHECK(it->value.IsBool());
    mix_group.loopback = it->value.GetBool();
  } else {
    mix_group.loopback = false;
  }

  it = value.FindMember(kJsonKeyOutputRate);
  if (it != value.MemberEnd()) {
    FX_DCHECK(it->value.IsUint());
    mix_group.output_rate = it->value.GetUint();
  } else {
    mix_group.output_rate = PipelineConfig::kDefaultMixGroupRate;
  }

  it = value.FindMember(kJsonKeyOutputChannels);
  if (it != value.MemberEnd()) {
    FX_DCHECK(it->value.IsUint());
    mix_group.output_channels = it->value.GetUint();
  } else {
    mix_group.output_channels = PipelineConfig::kDefaultMixGroupChannels;
  }

  return mix_group;
}

std::optional<audio_stream_unique_id_t> ParseDeviceIdFromJsonString(const rapidjson::Value& value) {
  FX_DCHECK(value.IsString());
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
    FX_DCHECK(captures == 16);
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
  FX_DCHECK(value.IsArray());
  StreamUsageSet supported_stream_types;
  for (const auto& stream_type : value.GetArray()) {
    FX_DCHECK(stream_type.IsString());
    const auto supported_usage = StreamUsageFromString(stream_type.GetString());
    FX_DCHECK(supported_usage);
    if (all_supported_usages) {
      all_supported_usages->insert(*supported_usage);
    }
    supported_stream_types.insert(*supported_usage);
  }
  return supported_stream_types;
}

fit::result<std::pair<std::optional<std::vector<audio_stream_unique_id_t>>,
                      DeviceConfig::OutputDeviceProfile>,
            std::string>
ParseOutputDeviceProfileFromJsonObject(const rapidjson::Value& value,
                                       StreamUsageSet* all_supported_usages) {
  FX_DCHECK(value.IsObject());

  auto device_id_it = value.FindMember(kJsonKeyDeviceId);
  FX_DCHECK(device_id_it != value.MemberEnd());

  auto device_id = ParseDeviceIdFromJsonValue(device_id_it->value);

  bool eligible_for_loopback = false;
  auto eligible_for_loopback_it = value.FindMember(kJsonKeyEligibleForLoopback);
  if (eligible_for_loopback_it != value.MemberEnd()) {
    FX_DCHECK(eligible_for_loopback_it->value.IsBool());
    eligible_for_loopback = eligible_for_loopback_it->value.GetBool();
  }

  auto independent_volume_control_it = value.FindMember(kJsonKeyIndependentVolumeControl);
  bool independent_volume_control = false;
  if (independent_volume_control_it != value.MemberEnd()) {
    FX_DCHECK(independent_volume_control_it->value.IsBool());
    independent_volume_control = independent_volume_control_it->value.GetBool();
  }

  float driver_gain_db = 0.0;
  auto driver_gain_db_it = value.FindMember(kJsonKeyDriverGainDb);
  if (driver_gain_db_it != value.MemberEnd()) {
    FX_DCHECK(driver_gain_db_it->value.IsNumber());
    driver_gain_db = driver_gain_db_it->value.GetDouble();
  }

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
      FX_DCHECK(false) << "Missing required stream usage set";
    }
  }
  auto& supported_stream_types_value = supported_stream_types_it->value;
  FX_DCHECK(supported_stream_types_value.IsArray());

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
        return fit::error("More than 1 loopback stage specified");
      }
      if (loopback_stages == 0) {
        return fit::error("Device supports loopback but no loopback point specified");
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

  return fit::ok(std::make_pair(
      device_id, DeviceConfig::OutputDeviceProfile(
                     eligible_for_loopback, std::move(supported_stream_types),
                     independent_volume_control, std::move(pipeline_config), driver_gain_db)));
}

// TODO(fxbug.dev/57804): Remove support for old config format once it is no longer in use.
std::vector<ThermalConfig::Entry> ParseThermalPolicyEntriesFromOldFormatJsonObject(
    const rapidjson::Value& value) {
  FX_DCHECK(value.IsObject());

  auto target_name_it = value.FindMember(kJsonKeyTargetName);
  FX_DCHECK(target_name_it != value.MemberEnd());
  FX_DCHECK(target_name_it->value.IsString());
  const auto* target_name = target_name_it->value.GetString();

  auto states_it = value.FindMember(kJsonKeyStates);
  FX_DCHECK(states_it != value.MemberEnd());
  FX_DCHECK(states_it->value.IsArray());
  auto states_array = states_it->value.GetArray();

  std::vector<ThermalConfig::Entry> entries;
  for (const auto& state : states_array) {
    FX_DCHECK(state.IsObject());

    auto trip_point_it = state.FindMember(kJsonKeyTripPoint);
    FX_DCHECK(trip_point_it != state.MemberEnd());
    FX_DCHECK(trip_point_it->value.IsUint());
    FX_DCHECK(trip_point_it->value.GetUint() >= 1);
    FX_DCHECK(trip_point_it->value.GetUint() <= 100);
    auto trip_point = trip_point_it->value.GetUint();

    auto config_it = state.FindMember(kJsonKeyConfig);
    FX_DCHECK(config_it != state.MemberEnd());
    rapidjson::StringBuffer config_buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(config_buf);
    config_it->value.Accept(writer);

    entries.push_back(ThermalConfig::Entry(
        ThermalConfig::TripPoint{trip_point, trip_point},
        {ThermalConfig::StateTransition(target_name, config_buf.GetString())}));
  }

  return entries;
}

ThermalConfig::Entry ParseThermalPolicyEntryFromNewFormatJsonObject(const rapidjson::Value& value) {
  FX_DCHECK(value.IsObject());

  auto trip_point_it = value.FindMember(kJsonKeyTripPoint);
  FX_DCHECK(trip_point_it != value.MemberEnd());

  FX_DCHECK(trip_point_it->value.IsObject());
  auto deactivate_below_it = trip_point_it->value.FindMember(kJsonKeyTripPointDeactivateBelow);
  FX_DCHECK(deactivate_below_it != trip_point_it->value.MemberEnd());
  FX_DCHECK(deactivate_below_it->value.IsUint());
  uint32_t deactivate_below = deactivate_below_it->value.GetUint();
  auto activate_at_it = trip_point_it->value.FindMember(kJsonKeyTripPointActivateAt);
  FX_DCHECK(activate_at_it != trip_point_it->value.MemberEnd());
  FX_DCHECK(activate_at_it->value.IsUint());
  uint32_t activate_at = activate_at_it->value.GetUint();
  FX_DCHECK(deactivate_below >= 1);
  FX_DCHECK(activate_at <= 100);

  auto transitions_it = value.FindMember(kJsonKeyStateTransitions);
  FX_DCHECK(transitions_it != value.MemberEnd());
  FX_DCHECK(transitions_it->value.IsArray());

  std::vector<ThermalConfig::StateTransition> transitions;
  for (const auto& transition : transitions_it->value.GetArray()) {
    FX_DCHECK(transition.IsObject());
    auto target_name_it = transition.FindMember(kJsonKeyTargetName);
    FX_DCHECK(target_name_it != value.MemberEnd());
    FX_DCHECK(target_name_it->value.IsString());
    const auto* target_name = target_name_it->value.GetString();

    auto config_it = transition.FindMember(kJsonKeyConfig);
    FX_DCHECK(config_it != transition.MemberEnd());
    rapidjson::StringBuffer config_buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(config_buf);
    config_it->value.Accept(writer);

    transitions.emplace_back(target_name, config_buf.GetString());
  }

  return ThermalConfig::Entry(
      ThermalConfig::TripPoint{.deactivate_below = deactivate_below, .activate_at = activate_at},
      transitions);
}

fit::result<void, std::string> ParseOutputDevicePoliciesFromJsonObject(
    const rapidjson::Value& output_device_profiles, ProcessConfigBuilder* config_builder) {
  FX_DCHECK(output_device_profiles.IsArray());

  StreamUsageSet all_supported_usages;
  for (const auto& output_device_profile : output_device_profiles.GetArray()) {
    auto result =
        ParseOutputDeviceProfileFromJsonObject(output_device_profile, &all_supported_usages);
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
      return fit::error(oss.str());
    }
  }
  return fit::ok();
}

fit::result<std::pair<std::optional<std::vector<audio_stream_unique_id_t>>,
                      DeviceConfig::InputDeviceProfile>>
ParseInputDeviceProfileFromJsonObject(const rapidjson::Value& value) {
  FX_DCHECK(value.IsObject());

  auto device_id_it = value.FindMember(kJsonKeyDeviceId);
  FX_DCHECK(device_id_it != value.MemberEnd());

  auto device_id = ParseDeviceIdFromJsonValue(device_id_it->value);

  auto rate_it = value.FindMember(kJsonKeyRate);
  FX_DCHECK(rate_it != value.MemberEnd());
  if (!rate_it->value.IsUint()) {
    return fit::error();
  }
  auto rate = rate_it->value.GetUint();

  float driver_gain_db = 0.0;
  auto driver_gain_db_it = value.FindMember(kJsonKeyDriverGainDb);
  if (driver_gain_db_it != value.MemberEnd()) {
    FX_DCHECK(driver_gain_db_it->value.IsNumber());
    driver_gain_db = driver_gain_db_it->value.GetDouble();
  }

  StreamUsageSet supported_stream_types;
  auto supported_stream_types_it = value.FindMember(kJsonKeySupportedStreamTypes);
  if (supported_stream_types_it != value.MemberEnd()) {
    supported_stream_types = ParseStreamUsageSetFromJsonArray(supported_stream_types_it->value);
    return fit::ok(std::make_pair(
        device_id,
        DeviceConfig::InputDeviceProfile(rate, std::move(supported_stream_types), driver_gain_db)));
  }

  return fit::ok(std::make_pair(device_id, DeviceConfig::InputDeviceProfile(rate, driver_gain_db)));
}

fit::result<> ParseInputDevicePoliciesFromJsonObject(const rapidjson::Value& input_device_profiles,
                                                     ProcessConfigBuilder* config_builder) {
  FX_DCHECK(input_device_profiles.IsArray());

  for (const auto& input_device_profile : input_device_profiles.GetArray()) {
    auto result = ParseInputDeviceProfileFromJsonObject(input_device_profile);
    if (result.is_error()) {
      return fit::error();
    }
    config_builder->AddDeviceProfile(result.take_value());
  }
  return fit::ok();
}

void ParseOldFormatThermalPolicy(const rapidjson::Value::ConstArray& thermal_policy_entries,
                                 ProcessConfigBuilder* config_builder) {
  // This is an artifical restriction to simplify parsing as the old format is phased out.
  FX_DCHECK(thermal_policy_entries.Size() == 1);
  const auto entries = ParseThermalPolicyEntriesFromOldFormatJsonObject(thermal_policy_entries[0]);
  for (const auto& entry : entries) {
    config_builder->AddThermalPolicyEntry(entry);
  }
}

void ParseNewFormatThermalPolicy(const rapidjson::Value::ConstArray& thermal_policy_entries,
                                 ProcessConfigBuilder* config_builder) {
  for (const auto& thermal_policy_entry : thermal_policy_entries) {
    config_builder->AddThermalPolicyEntry(
        ParseThermalPolicyEntryFromNewFormatJsonObject(thermal_policy_entry));
  }
}

void ParseThermalPolicyFromJsonObject(const rapidjson::Value& value,
                                      ProcessConfigBuilder* config_builder) {
  FX_DCHECK(value.IsArray());
  const auto thermal_policy_entries = value.GetArray();

  // Inspect the first entry to determine whether format is old or new. Entries in the old format
  // include the target name at the top level; entries in the new format do not.
  const auto& entry = thermal_policy_entries[0];
  if (entry.FindMember(kJsonKeyTargetName) != entry.MemberEnd()) {
    ParseOldFormatThermalPolicy(thermal_policy_entries, config_builder);
  } else {
    ParseNewFormatThermalPolicy(thermal_policy_entries, config_builder);
  }
}

}  // namespace

fit::result<ProcessConfig, std::string> ProcessConfigLoader::LoadProcessConfig(
    const char* filename) {
  std::string buffer;
  const auto file_exists = files::ReadFileToString(filename, &buffer);
  if (!file_exists) {
    return fit::error("File does not exist");
  }

  auto result = ParseProcessConfig(buffer);
  if (result.is_error()) {
    std::ostringstream oss;
    oss << "Parse error: " << result.error();
    return fit::error(oss.str());
  }

  return fit::ok(result.take_value());
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

  auto schema_result = LoadProcessConfigSchema();
  if (schema_result.is_error()) {
    return fit::error(schema_result.take_error());
  }
  rapidjson::SchemaValidator validator(schema_result.value());
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

  auto output_devices_it = doc.FindMember(kJsonKeyOutputDevices);
  if (output_devices_it != doc.MemberEnd()) {
    auto result =
        ParseOutputDevicePoliciesFromJsonObject(output_devices_it->value, &config_builder);
    if (result.is_error()) {
      std::ostringstream oss;
      oss << "Failed to parse output device policies: " << result.error();
      return fit::error(oss.str());
    }
  }
  auto input_devices_it = doc.FindMember(kJsonKeyInputDevices);
  if (input_devices_it != doc.MemberEnd()) {
    auto result = ParseInputDevicePoliciesFromJsonObject(input_devices_it->value, &config_builder);
    if (result.is_error()) {
      return fit::error("Failed to parse input device policies");
    }
  }

  auto thermal_policy_it = doc.FindMember(kJsonKeyThermalPolicy);
  if (thermal_policy_it != doc.MemberEnd()) {
    ParseThermalPolicyFromJsonObject(thermal_policy_it->value, &config_builder);
  }

  return fit::ok(config_builder.Build());
}

}  // namespace media::audio
