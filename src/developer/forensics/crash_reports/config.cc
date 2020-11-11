// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/config.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <memory>
#include <optional>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/lib/files/file.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"
#include "zircon/third_party/rapidjson/include/rapidjson/error/en.h"
#include "zircon/third_party/rapidjson/include/rapidjson/schema.h"
#include "zircon/third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics {
namespace crash_reports {
namespace {

const char kSchema[] = R"({
  "type": "object",
  "properties": {
    "crash_reporter": {
      "type": "object",
      "properties": {
        "daily_per_product_quota": {
          "type": "number"
        }
      },
      "required": [
        "daily_per_product_quota"
      ],
      "additionalProperties": false
    },
    "crash_server": {
      "type": "object",
      "properties": {
        "upload_policy": {
          "type": "string",
          "enum": [
            "disabled",
            "enabled",
            "read_from_privacy_settings"
          ]
        }
      },
      "required": [
        "upload_policy"
      ],
      "additionalProperties": false
    }
  },
  "required": [
    "crash_server"
  ],
  "additionalProperties": false
})";

bool CheckAgainstSchema(rapidjson::Document& doc) {
  // Check that the schema is actually valid.
  rapidjson::Document sd;
  rapidjson::ParseResult ok = sd.Parse(kSchema);
  if (!ok) {
    FX_LOGS(ERROR) << "invalid JSON schema for config at offset " << ok.Offset() << " "
                   << rapidjson::GetParseError_En(ok.Code());
    return false;
  }

  // Check the document against the schema.
  rapidjson::SchemaDocument schema(sd);
  rapidjson::SchemaValidator validator(schema);
  if (!doc.Accept(validator)) {
    rapidjson::StringBuffer sb;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    FX_LOGS(ERROR) << "config does not match schema, violating '"
                   << validator.GetInvalidSchemaKeyword() << "' rule";
    return false;
  }
  return true;
}

// Functions for parsing the config _after_ |doc| has been checked against the schema.
//
// These functions will crash if |doc| is is malformed.
std::optional<uint64_t> ParseCrashReporterConfig(const rapidjson::Document& doc) {
  if (!doc.HasMember(kCrashReporterKey)) {
    return std::nullopt;
  }

  return doc[kCrashReporterKey][kDailyPerProductQuotaKey].GetUint64();
}

std::optional<CrashServerConfig> ParseCrashServerConfig(const rapidjson::Document& doc) {
  CrashServerConfig config;

  const std::string upload_policy = doc[kCrashServerKey][kCrashServerUploadPolicyKey].GetString();
  if (upload_policy == "disabled") {
    config.upload_policy = CrashServerConfig::UploadPolicy::DISABLED;
  } else if (upload_policy == "enabled") {
    config.upload_policy = CrashServerConfig::UploadPolicy::ENABLED;
  } else if (upload_policy == "read_from_privacy_settings") {
    config.upload_policy = CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS;
  } else {
    FX_LOGS(ERROR) << "unknown upload policy " << upload_policy;
    return std::nullopt;
  }

  return config;
}

}  // namespace

std::optional<Config> ParseConfig(const std::string& filepath) {
  std::string json;
  if (!files::ReadFileToString(filepath, &json)) {
    FX_LOGS(ERROR) << "error reading config file at " << filepath;
    return std::nullopt;
  }

  rapidjson::Document doc;
  rapidjson::ParseResult ok = doc.Parse(json.c_str());
  if (!ok) {
    FX_LOGS(ERROR) << "error parsing config as JSON at offset " << ok.Offset() << " "
                   << rapidjson::GetParseError_En(ok.Code());
    return std::nullopt;
  }

  if (!CheckAgainstSchema(doc)) {
    return std::nullopt;
  }

  Config config{.daily_per_product_quota = ParseCrashReporterConfig(doc)};
  if (auto crash_server = ParseCrashServerConfig(doc); crash_server.has_value()) {
    config.crash_server = std::move(crash_server.value());
  } else {
    return std::nullopt;
  }

  // If crash reports won't be uploaded, there shouldn't be a quota in the config.
  if (config.crash_server.upload_policy == CrashServerConfig::UploadPolicy::DISABLED) {
    FX_CHECK(config.daily_per_product_quota == std::nullopt);
  }

  return config;
}

std::string ToString(const CrashServerConfig::UploadPolicy upload_policy) {
  switch (upload_policy) {
    case CrashServerConfig::UploadPolicy::DISABLED:
      return "DISABLED";
    case CrashServerConfig::UploadPolicy::ENABLED:
      return "ENABLED";
    case CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS:
      return "READ_FROM_PRIVACY_SETTINGS";
  }
}

}  // namespace crash_reports
}  // namespace forensics
