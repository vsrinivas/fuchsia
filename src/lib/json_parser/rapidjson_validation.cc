// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/json_parser/rapidjson_validation.h"

#include <lib/fitx/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <memory>

#include <rapidjson/error/en.h>

namespace json_parser {

std::string InitSchemaError::ToString() const {
  std::stringstream s;
  s << "Schema validation spec is not valid JSON"
    << ": offset " << offset << ", " << rapidjson::GetParseError_En(code);
  return s.str();
}

fitx::result<InitSchemaError, rapidjson::SchemaDocument> InitSchema(std::string_view json) {
  rapidjson::Document schema_document;
  if (schema_document.Parse(json.data(), json.size()).HasParseError()) {
    auto offset = schema_document.GetErrorOffset();
    auto code = schema_document.GetParseError();
    return fitx::error(InitSchemaError{.offset = offset, .code = code});
  }
  return fitx::ok(rapidjson::SchemaDocument(schema_document));
}

fitx::result<std::string> ValidateSchema(const rapidjson::Value& value,
                                         const rapidjson::SchemaDocument& schema,
                                         std::string_view value_name) {
  rapidjson::SchemaValidator validator(schema);
  if (!value.Accept(validator)) {
    rapidjson::StringBuffer uri_buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(uri_buffer);
    std::stringstream s;
    s << "Incorrect schema ";

    if (!value_name.empty()) {
      s << "of \"" << value_name << "\" ";
    }
    s << "at " << uri_buffer.GetString()
      << " , schema violation: " << validator.GetInvalidSchemaKeyword();
    return fitx::error(s.str());
  }
  return fitx::ok();
}

}  // namespace json_parser
