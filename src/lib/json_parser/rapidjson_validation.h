// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_JSON_PARSER_RAPIDJSON_VALIDATION_H_
#define SRC_LIB_JSON_PARSER_RAPIDJSON_VALIDATION_H_

#include <memory>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>

#include "src/lib/fxl/strings/string_view.h"

namespace json_parser {

// Use `InitSchemaDeprecatedDeprecated`.
std::unique_ptr<rapidjson::SchemaDocument> InitSchema(fxl::StringView json);

// We are in the process of adding new API which will return error rather than loggign it. In the
// meantime keep using this function.
// Build a SchemaDocument from a json encoded string.
// Returns null if |json| is invalid.
std::unique_ptr<rapidjson::SchemaDocument> InitSchemaDeprecated(fxl::StringView json);

// Use `ValidateSchemaDeprecatedDeprecated`.
bool ValidateSchema(const rapidjson::Value& value, const rapidjson::SchemaDocument& schema,
                    fxl::StringView value_name = "");

// We are in the process of adding new API which will return error rather than loggign it. In the
// meantime keep using this function.
// Validate that the given json value match the given schema.
// If not empty, |value_name| is printed in the log should the validation fail.
bool ValidateSchemaDeprecated(const rapidjson::Value& value,
                              const rapidjson::SchemaDocument& schema,
                              fxl::StringView value_name = "");

}  // namespace json_parser

#endif  // SRC_LIB_JSON_PARSER_RAPIDJSON_VALIDATION_H_
