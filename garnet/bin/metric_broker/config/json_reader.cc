// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include "json_reader.h"

#include <utility>
#include <vector>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace broker_service {

JsonReader::JsonReader(rapidjson::Document document, rapidjson::SchemaDocument* schema)
    : document_(std::move(document)), validator_(*schema) {}

bool JsonReader::Validate() {
  if (!document_.Accept(validator_)) {
    rapidjson::StringBuffer buffer;

    // Check if there is an invalid schema.
    validator_.GetInvalidSchemaPointer().StringifyUriFragment(buffer);
    rapidjson::StringBuffer doc;
    validator_.GetInvalidDocumentPointer().StringifyUriFragment(doc);
    std::string schema_error = "Invalid schema: " + std::string(buffer.GetString()) +
                               "\n   keyword: " + validator_.GetInvalidSchemaKeyword() +
                               "\n   document: " + doc.GetString();
    error_messages_.emplace_back(schema_error);

    return false;
  }

  return true;
}

}  // namespace broker_service
