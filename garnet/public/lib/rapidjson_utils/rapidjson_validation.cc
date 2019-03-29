// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/rapidjson_utils/rapidjson_validation.h"

#include <src/lib/fxl/logging.h>
#include <rapidjson/error/en.h>

namespace rapidjson_utils {

std::unique_ptr<rapidjson::SchemaDocument> InitSchema(fxl::StringView json) {
  rapidjson::Document schema_document;
  if (schema_document.Parse(json.data(), json.size()).HasParseError()) {
    auto offset = schema_document.GetErrorOffset();
    auto code = schema_document.GetParseError();
    FXL_LOG(ERROR) << "Schema validation spec itself is not valid JSON"
                   << ": offset " << offset << ", "
                   << rapidjson::GetParseError_En(code);
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
                    const rapidjson::SchemaDocument& schema,
                    fxl::StringView value_name) {
  rapidjson::SchemaValidator validator(schema);
  if (!value.Accept(validator)) {
    rapidjson::StringBuffer uri_buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(uri_buffer);
    std::string extra_log_info;
    if (!value_name.empty()) {
      extra_log_info = "of \"" + value_name.ToString() + "\" ";
    }
    FXL_LOG(ERROR) << "Incorrect schema " << extra_log_info << "at "
                   << uri_buffer.GetString() << " , schema violation: "
                   << validator.GetInvalidSchemaKeyword();
    return false;
  }
  return true;
}

}  // namespace rapidjson_utils
