// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_JSON_PARSER_RAPIDJSON_VALIDATION_H_
#define SRC_LIB_JSON_PARSER_RAPIDJSON_VALIDATION_H_

#include <lib/fitx/result.h>

#include <memory>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>

#include "src/lib/fxl/strings/string_view.h"

namespace json_parser {

// Error for `InitSchema` function.
struct InitSchemaError {
  // Offset in schema json where error occured.
  size_t offset;

  // Call `rapidjson::GetParseError_En` on error code to get string representation.
  rapidjson::ParseErrorCode code;

  std::string ToString() const;
};

// Build a SchemaDocument from a json encoded string.
fitx::result<InitSchemaError, rapidjson::SchemaDocument> InitSchema(fxl::StringView json);

// Validate that the given json value match the given schema.
// Returns validation error on error.
fitx::result<std::string> ValidateSchema(const rapidjson::Value& value,
                                         const rapidjson::SchemaDocument& schema,
                                         fxl::StringView value_name = "");

}  // namespace json_parser

#endif  // SRC_LIB_JSON_PARSER_RAPIDJSON_VALIDATION_H_
