// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include <lib/fxl/files/file.h>
#include <lib/syslog/cpp/logger.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <zircon/errors.h>

namespace fuchsia {
namespace crash {
namespace {

const char kSchema[] = R"({
  "type": "object",
  "properties": {
    "local_crashpad_database_path": {
      "type": "string"
    },
    "enable_upload_to_crash_server": {
      "type": "boolean"
    }
  },
  "required": [
    "local_crashpad_database_path",
    "enable_upload_to_crash_server"
  ],
  "additionalProperties": false
})";

const char kLocalCrashpadDatabasePathKey[] = "local_crashpad_database_path";
const char kEnableUploadToCrashServerKey[] = "enable_upload_to_crash_server";

bool CheckSchema(rapidjson::Document& doc) {
  rapidjson::Document sd;
  if (sd.Parse(kSchema).HasParseError()) {
    FX_LOGS(ERROR) << "invalid JSON schema for config";
    return false;
  }
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
    FX_LOGS(ERROR) << "error parsing config as JSON: "
                   << rapidjson::GetParseError_En(ok.Code()) << "("
                   << ok.Offset() << ")";
    return ZX_ERR_INTERNAL;
  }

  if (!CheckSchema(doc)) {
    return ZX_ERR_INTERNAL;
  }

  // It is safe to directly access the fields as the keys are marked as required
  // and we have checked the config against the schema.
  config->local_crashpad_database_path =
      doc[kLocalCrashpadDatabasePathKey].GetString();
  config->enable_upload_to_crash_server =
      doc[kEnableUploadToCrashServerKey].GetBool();

  return ZX_OK;
}

}  // namespace crash
}  // namespace fuchsia
