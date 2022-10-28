// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/config.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "src/lib/files/file.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/error/en.h"
#include "third_party/rapidjson/include/rapidjson/error/error.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics::feedback {
namespace {

const char kSchema[] = R"({
  "type": "object",
  "properties": {
    "enable_data_redaction": {
      "type": "boolean"
    },
    "enable_hourly_snapshots": {
      "type": "boolean"
    },
    "enable_limit_inspect_data": {
      "type": "boolean"
    }
  },
  "required": [
    "enable_data_redaction",
    "enable_hourly_snapshots",
    "enable_limit_inspect_data"
  ],
  "additionalProperties": false
})";

std::optional<BuildTypeConfig> ReadConfig(const std::string& filepath) {
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
  if (const rapidjson::ParseResult result = schema.Parse(kSchema); !result) {
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

  return BuildTypeConfig{
      .enable_data_redaction = config["enable_data_redaction"].GetBool(),
      .enable_hourly_snapshots = config["enable_hourly_snapshots"].GetBool(),
      .enable_limit_inspect_data = config["enable_limit_inspect_data"].GetBool(),
  };
}

}  // namespace

std::optional<BuildTypeConfig> GetBuildTypeConfig(const std::string& default_path,
                                                  const std::string& override_path) {
  std::optional<BuildTypeConfig> config;
  if (files::IsFile(override_path)) {
    if (config = ReadConfig(override_path); !config.has_value()) {
      FX_LOGS(ERROR) << "Failed to read override build type config file at " << override_path
                     << " - falling back to default";
    }
  }

  if (!config) {
    if (config = ReadConfig(default_path); !config) {
      FX_LOGS(ERROR) << "Failed to read default build type config file at " << default_path;
    }
  }

  return config;
}

std::optional<crash_reports::Config> GetCrashReportsConfig(const std::string& default_path,
                                                           const std::string& override_path) {
  std::optional<crash_reports::Config> config;
  if (files::IsFile(override_path)) {
    if (config = crash_reports::ParseConfig(override_path); !config) {
      FX_LOGS(ERROR) << "Failed to read override config file at " << override_path
                     << " - falling back to default config file";
    }
  }

  if (!config) {
    if (config = crash_reports::ParseConfig(default_path); !config) {
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
