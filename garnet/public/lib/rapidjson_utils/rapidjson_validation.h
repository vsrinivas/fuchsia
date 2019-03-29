// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_RAPIDJSON_UTILS_RAPIDJSON_VALIDATION_H_
#define LIB_RAPIDJSON_UTILS_RAPIDJSON_VALIDATION_H_

#include <memory>

#include <src/lib/fxl/strings/string_view.h>
#include <rapidjson/document.h>
#include <rapidjson/schema.h>

namespace rapidjson_utils {

// Build a SchemaDocument from a json encoded string.
// Returns null if |json| is invalid.
std::unique_ptr<rapidjson::SchemaDocument> InitSchema(fxl::StringView json);

// Validate that the given json value match the given schema.
// If not empty, |value_name| is printed in the log should the validation fail.
bool ValidateSchema(const rapidjson::Value& value,
                    const rapidjson::SchemaDocument& schema,
                    fxl::StringView value_name = "");

}  // namespace rapidjson_utils

#endif  // LIB_RAPIDJSON_UTILS_RAPIDJSON_VALIDATION_H_
