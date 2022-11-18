// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/config.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/file.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/error/en.h"
#include "third_party/rapidjson/include/rapidjson/error/error.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics::feedback {
namespace {

template <typename T>
std::optional<T> ReadConfig(const std::string& schema_str,
                            std::function<std::optional<T>(const rapidjson::Document&)> convert_fn,
                            const std::string& filepath) {
  std::string config_str;
  if (!files::ReadFileToString(filepath, &config_str)) {
    FX_LOGS(ERROR) << "Error reading config file at " << filepath;
    return std::nullopt;
  }

  rapidjson::Document config;
  if (const rapidjson::ParseResult result = config.Parse(config_str.c_str()); !result) {
    FX_LOGS(ERROR) << "Error parsing config as JSON at offset " << result.Offset() << " "
                   << rapidjson::GetParseError_En(result.Code());
    return std::nullopt;
  }

  rapidjson::Document schema;
  if (const rapidjson::ParseResult result = schema.Parse(schema_str); !result) {
    FX_LOGS(ERROR) << "Error parsing config schema at offset " << result.Offset() << " "
                   << rapidjson::GetParseError_En(result.Code());
    return std::nullopt;
  }

  rapidjson::SchemaDocument schema_doc(schema);
  if (rapidjson::SchemaValidator validator(schema_doc); !config.Accept(validator)) {
    rapidjson::StringBuffer buf;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(buf);
    FX_LOGS(ERROR) << "Config does not match schema, violating '"
                   << validator.GetInvalidSchemaKeyword() << "' rule";
    return std::nullopt;
  }

  return convert_fn(config);
}

template <typename T>
std::optional<T> GetConfig(const std::string& schema_str,
                           std::function<std::optional<T>(const rapidjson::Document&)> convert_fn,
                           const std::string& config_type, const std::string& default_path,
                           const std::string& override_path) {
  std::optional<T> config;
  if (files::IsFile(override_path)) {
    if (config = ReadConfig<T>(schema_str, convert_fn, override_path); !config.has_value()) {
      FX_LOGS(ERROR) << "Failed to read override " << config_type << " config file at "
                     << override_path;
    }
  }

  if (!config.has_value()) {
    if (config = ReadConfig<T>(schema_str, convert_fn, default_path); !config.has_value()) {
      FX_LOGS(ERROR) << "Failed to read default " << config_type << " config file at "
                     << default_path;
    }
  }

  return config;
}

constexpr char kBoardConfigSchema[] = R"({
    "type": "object",
    "properties": {
       "persisted_logs_num_files": {
           "type": "number"
       },
       "persisted_logs_total_size_kib": {
           "type": "number"
       },
       "snapshot_persistence_max_tmp_size_mib": {
           "type": "number"
       },
       "snapshot_persistence_max_cache_size_mib": {
           "type": "number"
       }
    },
    "required": [
       "persisted_logs_num_files",
       "persisted_logs_total_size_kib",
       "snapshot_persistence_max_tmp_size_mib",
       "snapshot_persistence_max_cache_size_mib"
    ],
    "additionalProperties": false
})";

std::optional<BoardConfig> ParseBoardConfig(const rapidjson::Document& json) {
  BoardConfig config;
  if (const int64_t num_files = json["persisted_logs_num_files"].GetInt64(); num_files > 0) {
    config.persisted_logs_num_files = num_files;
  } else {
    FX_LOGS(ERROR) << "Can't use non-positive number of files for system log persistence: "
                   << num_files;
    return std::nullopt;
  }

  if (const int64_t total_size_kib = json["persisted_logs_total_size_kib"].GetInt64();
      total_size_kib > 0) {
    config.persisted_logs_total_size = StorageSize::Kilobytes(total_size_kib);
  } else {
    FX_LOGS(ERROR) << "Can't use non-positive size for system log persistence: " << total_size_kib;
    return std::nullopt;
  }

  if (const int64_t max_tmp_size_mib = json["snapshot_persistence_max_tmp_size_mib"].GetInt64();
      max_tmp_size_mib > 0) {
    config.snapshot_persistence_max_tmp_size = StorageSize::Megabytes(max_tmp_size_mib);
  } else {
    config.snapshot_persistence_max_tmp_size = std::nullopt;
  }

  if (const int64_t max_cache_size_mib = json["snapshot_persistence_max_cache_size_mib"].GetInt64();
      max_cache_size_mib > 0) {
    config.snapshot_persistence_max_cache_size = StorageSize::Megabytes(max_cache_size_mib);
  } else {
    config.snapshot_persistence_max_cache_size = std::nullopt;
  }

  return config;
}

const char kBuildTypeConfigSchema[] = R"({
  "type": "object",
  "properties": {
    "enable_data_redaction": {
      "type": "boolean"
    },
    "enable_limit_inspect_data": {
      "type": "boolean"
    }
  },
  "required": [
    "enable_data_redaction",
    "enable_limit_inspect_data"
  ],
  "additionalProperties": false
})";

std::optional<BuildTypeConfig> ParseBuildTypeConfig(const rapidjson::Document& json) {
  return BuildTypeConfig{
      .enable_data_redaction = json["enable_data_redaction"].GetBool(),
      .enable_limit_inspect_data = json["enable_limit_inspect_data"].GetBool(),
  };
}

}  // namespace

std::optional<BoardConfig> GetBoardConfig(const std::string& default_path,
                                          const std::string& override_path) {
  return GetConfig<BoardConfig>(kBoardConfigSchema, ParseBoardConfig, "board", default_path,
                                override_path);
}

std::optional<BuildTypeConfig> GetBuildTypeConfig(const std::string& default_path,
                                                  const std::string& override_path) {
  return GetConfig<BuildTypeConfig>(kBuildTypeConfigSchema, ParseBuildTypeConfig, "build type",
                                    default_path, override_path);
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
