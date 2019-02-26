// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_JSON_SCHEMA_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_JSON_SCHEMA_H_

#include <memory>

#include <lib/fxl/strings/string_view.h>
#include <rapidjson/document.h>
#include <rapidjson/schema.h>

namespace json_schema {

// Build a SchemaDocument from a json encoded string.
std::unique_ptr<rapidjson::SchemaDocument> InitSchema(fxl::StringView json);

// Validate that the given json value match the given schema.
bool ValidateSchema(const rapidjson::Value& value,
                    const rapidjson::SchemaDocument& schema);

};  // namespace json_schema

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_JSON_SCHEMA_H_
