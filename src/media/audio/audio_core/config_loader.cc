// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/config_loader.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

#include "src/media/audio/audio_core/schema/volume_curve_schema.inl"

namespace media::audio {

namespace {

rapidjson::SchemaDocument LoadSchema() {
  rapidjson::Document schema_doc;
  const rapidjson::ParseResult result = schema_doc.Parse(kVolumeCurveSchema.c_str());
  FXL_CHECK(!result.IsError()) << rapidjson::GetParseError_En(result.Code());
  return rapidjson::SchemaDocument(schema_doc);
}

}  // namespace

std::optional<VolumeCurve> ConfigLoader::LoadVolumeCurveFromDisk(const char* filename) {
  std::string buffer;
  const auto file_exists = files::ReadFileToString(filename, &buffer);
  if (!file_exists) {
    return std::nullopt;
  }

  rapidjson::Document doc;
  const rapidjson::ParseResult parse_res = doc.ParseInsitu(buffer.data());
  if (parse_res.IsError()) {
    FXL_LOG(FATAL) << "Parse error (" << rapidjson::GetParseError_En(parse_res.Code())
                   << ") when reading volume curve.";
  }

  const auto schema = LoadSchema();
  rapidjson::SchemaValidator validator(schema);
  if (!doc.Accept(validator)) {
    FXL_LOG(FATAL) << "Schema validation error when reading volume curve.";
  }

  std::vector<VolumeCurve::VolumeMapping> mappings;
  for (const auto& mapping : doc["volume_curve"].GetArray()) {
    mappings.emplace_back(mapping["level"].GetFloat(), mapping["db"].GetFloat());
  }

  auto curve_result = VolumeCurve::FromMappings(std::move(mappings));
  if (!curve_result.is_ok()) {
    FXL_LOG(FATAL) << "Invalid volume curve; error: " << curve_result.take_error();
  }

  return {curve_result.take_value()};
}

}  // namespace media::audio
