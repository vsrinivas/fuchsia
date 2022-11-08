// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/config.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/error/en.h"
#include "third_party/rapidjson/include/rapidjson/error/error.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics::feedback {
namespace {

constexpr char kBoardConfigSchema[] = R"({
    "type": "object",
    "properties": {
       "persisted_logs_num_files": {
           "type": "number"
       },
       "persisted_logs_total_size_kib": {
           "type": "number"
       }
    },
    "required": [
       "persisted_logs_num_files",
       "persisted_logs_total_size_kib"
    ],
    "additionalProperties": false
})";

std::optional<BoardConfig> ReadBoardConfig(const std::string& filepath) {
  std::string config_str;
  if (!files::ReadFileToString(filepath, &config_str)) {
    FX_LOGS(ERROR) << "Error reading build type config file at " << filepath;
    return std::nullopt;
  }

  rapidjson::Document config;
  if (const rapidjson::ParseResult result = config.Parse(config_str.c_str()); !result) {
    FX_LOGS(ERROR) << "Error parsing build type config as JSON at offset " << result.Offset() << " "
                   << rapidjson::GetParseError_En(result.Code());
    return std::nullopt;
  }

  rapidjson::Document schema;
  if (const rapidjson::ParseResult result = schema.Parse(kBoardConfigSchema); !result) {
    FX_LOGS(ERROR) << "Error parsing build type config schema at offset " << result.Offset() << " "
                   << rapidjson::GetParseError_En(result.Code());
    return std::nullopt;
  }

  rapidjson::SchemaDocument schema_doc(schema);
  if (rapidjson::SchemaValidator validator(schema_doc); !config.Accept(validator)) {
    rapidjson::StringBuffer buf;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(buf);
    FX_LOGS(ERROR) << "Build type config does not match schema, violating '"
                   << validator.GetInvalidSchemaKeyword() << "' rule";
    return std::nullopt;
  }

  BoardConfig device_config;
  if (const int64_t num_files = config["persisted_logs_num_files"].GetInt64(); num_files > 0) {
    device_config.persisted_logs_num_files = num_files;
  } else {
    FX_LOGS(ERROR) << "Can't use non-positive number of files for system log persistence: "
                   << num_files;
    return std::nullopt;
  }

  if (const int64_t total_size_kib = config["persisted_logs_total_size_kib"].GetInt64();
      total_size_kib > 0) {
    device_config.persisted_logs_total_size = StorageSize::Kilobytes(total_size_kib);
  } else {
    FX_LOGS(ERROR) << "Can't use non-positive size for system log persistence: " << total_size_kib;
    return std::nullopt;
  }

  return device_config;
}

}  // namespace

std::optional<BoardConfig> GetBoardConfig(const std::string& default_path,
                                          const std::string& override_path) {
  std::optional<BoardConfig> config;
  if (files::IsFile(override_path)) {
    if (config = ReadBoardConfig(override_path); !config.has_value()) {
      FX_LOGS(ERROR) << "Failed to read override board config file at " << override_path
                     << " - falling back to default";
    }
  }

  if (!config.has_value()) {
    if (config = ReadBoardConfig(default_path); !config.has_value()) {
      FX_LOGS(ERROR) << "Failed to read default board config file at " << default_path;
    }
  }

  return config;
}

std::optional<crash_reports::Config> GetCrashReportsConfig(const std::string& default_path,
                                                           const std::string& override_path) {
  std::optional<crash_reports::Config> config;
  if (files::IsFile(override_path)) {
    if (config = crash_reports::ParseConfig(override_path); !config.has_value()) {
      FX_LOGS(ERROR) << "Failed to read override config file at " << override_path
                     << " - falling back to default config file";
    }
  }

  if (!config.has_value()) {
    if (config = crash_reports::ParseConfig(default_path); !config.has_value()) {
      FX_LOGS(ERROR) << "Failed to read default config file at " << default_path;
    }
  }

  return config;
}

std::optional<feedback_data::Config> GetFeedbackDataConfig(const std::string& path) {
  feedback_data::Config config;
  if (const zx_status_t status = feedback_data::ParseConfig(path, &config); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read config file at " << path;
    return std::nullopt;
  }

  return config;
}

}  // namespace forensics::feedback
