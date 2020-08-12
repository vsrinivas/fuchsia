// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/config.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include "src/lib/files/file.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"
#include "zircon/third_party/rapidjson/include/rapidjson/error/en.h"
#include "zircon/third_party/rapidjson/include/rapidjson/schema.h"
#include "zircon/third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics {
namespace feedback_data {
namespace {

const char kSchema[] = R"({
  "type": "object",
  "properties": {
    "annotation_allowlist": {
      "type": "array",
      "items": {
        "type": "string"
      },
      "uniqueItems": true
    },
    "attachment_allowlist": {
      "type": "array",
      "items": {
        "type": "string"
      },
      "uniqueItems": true
    }
  },
  "required": [
    "annotation_allowlist",
    "attachment_allowlist"
  ],
  "additionalProperties": false
})";

const char kAnnotationWhitelistKey[] = "annotation_allowlist";
const char kAttachmentWhitelistKey[] = "attachment_allowlist";

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
    FX_LOGS(ERROR) << "error parsing config as JSON at offset " << ok.Offset() << " "
                   << rapidjson::GetParseError_En(ok.Code());
    return ZX_ERR_INTERNAL;
  }

  if (!CheckAgainstSchema(doc)) {
    return ZX_ERR_INTERNAL;
  }

  // We use a local config to only set the out argument after all the checks.
  Config local_config = {};
  // It is safe to directly access the field as the keys are marked as required and we have checked
  // the config against the schema.
  for (const auto& annotation_key : doc[kAnnotationWhitelistKey].GetArray()) {
    // No need to warn on duplicates as the schema enforces "uniqueItems".
    local_config.annotation_allowlist.insert(annotation_key.GetString());
  }
  for (const auto& attachment_key : doc[kAttachmentWhitelistKey].GetArray()) {
    // No need to warn on duplicates as the schema enforces "uniqueItems".
    local_config.attachment_allowlist.insert(attachment_key.GetString());
  }

  *config = local_config;
  return ZX_OK;
}

}  // namespace feedback_data
}  // namespace forensics
