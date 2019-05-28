// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/config.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "src/lib/files/file.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/error/en.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace fuchsia {
namespace crash {
namespace {

const char kSchema[] = R"({
  "type": "object",
  "properties": {
    "local_crashpad_database_path": {
      "type": "string"
    },
    "max_crashpad_database_size_in_kb": {
      "type": "integer"
    },
    "enable_upload_to_crash_server": {
      "type": "boolean"
    },
    "crash_server_url": {
      "type": "string"
    },
    "feedback_data_collection_timeout_in_milliseconds": {
      "type": "integer"
    }
  },
  "required": [
    "local_crashpad_database_path",
    "max_crashpad_database_size_in_kb",
    "enable_upload_to_crash_server",
    "feedback_data_collection_timeout_in_milliseconds"
  ],
  "additionalProperties": false
})";

const char kLocalCrashpadDatabasePathKey[] = "local_crashpad_database_path";
const char kMaxDatabaseSizeInKbKey[] = "max_crashpad_database_size_in_kb";
const char kEnableUploadToCrashServerKey[] = "enable_upload_to_crash_server";
const char kCrashServerUrlKey[] = "crash_server_url";
const char kFeedbackDataCollectionTimeoutInSecondsKey[] =
    "feedback_data_collection_timeout_in_milliseconds";

bool CheckAgainstSchema(rapidjson::Document& doc) {
  // Check that the schema is actually valid.
  rapidjson::Document sd;
  rapidjson::ParseResult ok = sd.Parse(kSchema);
  if (!ok) {
    FX_LOGS(ERROR) << "invalid JSON schema for config at offset " << ok.Offset()
                   << " " << rapidjson::GetParseError_En(ok.Code());
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

}  // namespace

zx_status_t ParseConfig(const std::string& filepath, Config* config) {
  std::string json;
  if (!files::ReadFileToString(filepath, &json)) {
    FX_LOGS(ERROR) << "error reading config file at " << filepath;
    return ZX_ERR_IO;
  }

  rapidjson::Document doc;
  rapidjson::ParseResult ok = doc.Parse(json.c_str());
  if (!ok) {
    FX_LOGS(ERROR) << "error parsing config as JSON at offset " << ok.Offset()
                   << " " << rapidjson::GetParseError_En(ok.Code());
    return ZX_ERR_INTERNAL;
  }

  if (!CheckAgainstSchema(doc)) {
    return ZX_ERR_INTERNAL;
  }

  // We use a local config to only set the out argument after all the checks.
  Config local_config;
  // It is safe to directly access these fields for which the keys are marked as
  // required as we have checked the config against the schema.
  local_config.local_crashpad_database_path =
      doc[kLocalCrashpadDatabasePathKey].GetString();
  local_config.max_crashpad_database_size_in_kb =
      doc[kMaxDatabaseSizeInKbKey].GetUint();
  local_config.enable_upload_to_crash_server =
      doc[kEnableUploadToCrashServerKey].GetBool();
  local_config.feedback_data_collection_timeout_in_milliseconds =
      doc[kFeedbackDataCollectionTimeoutInSecondsKey].GetUint();

  if (local_config.enable_upload_to_crash_server) {
    if (!doc.HasMember(kCrashServerUrlKey)) {
      FX_LOGS(ERROR)
          << "missing crash server URL in config with upload enabled";
      return ZX_ERR_INTERNAL;
    }
    local_config.crash_server_url =
        std::make_unique<std::string>(doc[kCrashServerUrlKey].GetString());
  } else if (doc.HasMember(kCrashServerUrlKey)) {
    FX_LOGS(WARNING) << "crash server URL set in config with upload disabled, "
                        "ignoring value";
  }

  *config = std::move(local_config);
  return ZX_OK;
}

}  // namespace crash
}  // namespace fuchsia
