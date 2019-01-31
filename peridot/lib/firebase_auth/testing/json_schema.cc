// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/json_schema.h"

#include <lib/fxl/logging.h>

namespace json_schema {

std::unique_ptr<rapidjson::SchemaDocument> InitSchema(fxl::StringView json) {
  rapidjson::Document schema_document;
  if (schema_document.Parse(json.data(), json.size()).HasParseError()) {
    FXL_LOG(ERROR) << "Schema validation spec itself is not valid JSON.";
    return nullptr;
  }
  auto schema = std::make_unique<rapidjson::SchemaDocument>(schema_document);
  rapidjson::SchemaValidator validator(*schema);
  if (!validator.IsValid()) {
    FXL_LOG(ERROR) << "Schema validation spec itself is not a valid schema.";
    return nullptr;
  }
  return schema;
}

bool ValidateSchema(const rapidjson::Value& value,
                    const rapidjson::SchemaDocument& schema) {
  rapidjson::SchemaValidator validator(schema);
  if (!value.Accept(validator)) {
    rapidjson::StringBuffer uri_buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(uri_buffer);
    FXL_LOG(ERROR) << "Incorrect schema at " << uri_buffer.GetString()
                   << " , schema violation: "
                   << validator.GetInvalidSchemaKeyword();
    return false;
  }
  return true;
}

}  // namespace json_schema
