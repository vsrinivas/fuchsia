// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

#include "src/media/audio/audio_core/schema/audio_core_config_schema.inl"

namespace media::audio {

namespace {

rapidjson::SchemaDocument LoadProcessConfigSchema() {
  rapidjson::Document schema_doc;
  const rapidjson::ParseResult result = schema_doc.Parse(kAudioCoreConfigSchema);
  FXL_CHECK(!result.IsError()) << rapidjson::GetParseError_En(result.Code());
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
                   << ") when reading " << filename;
  }

  const auto schema = LoadProcessConfigSchema();
  rapidjson::SchemaValidator validator(schema);
  if (!doc.Accept(validator)) {
    FXL_LOG(FATAL) << "Schema validation error when reading " << filename;
  }

  auto curve_result = ParseVolumeCurveFromJsonObject(doc["volume_curve"]);
  if (!curve_result.is_ok()) {
    FXL_LOG(FATAL) << "Invalid volume curve; error: " << curve_result.take_error();
  }

  return {ProcessConfig::Builder().SetDefaultVolumeCurve(curve_result.take_value()).Build()};
}

}  // namespace media::audio
