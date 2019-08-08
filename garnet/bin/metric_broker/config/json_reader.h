// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_JSON_READER_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_JSON_READER_H_

#include <string>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/schema.h"

namespace broker_service {

// This class provides a bae class for validating JSON documents based on a schema.
//
// This class is thread_compatible.
// This class is not copyable or moveable.
// This class is not assignable.
class JsonReader {
 public:
  JsonReader(rapidjson::Document document, rapidjson::SchemaDocument* schema);
  JsonReader(const JsonReader&) = delete;
  JsonReader(JsonReader&&) = delete;
  JsonReader& operator=(const JsonReader&) = delete;
  JsonReader& operator=(JsonReader&&) = delete;
  virtual ~JsonReader() = default;

  // Returns true if |document_| complies with |schema_|.
  // Needs to be called before any Read* Method.
  bool Validate();

  // Returns true if there has been no error parsing so far.
  [[nodiscard]] bool IsOk() const { return error_messages_.empty(); }

  // Returns the list of errors found while parsing the json.
  const std::vector<std::string>& error_messages() { return error_messages_; }

 protected:
  rapidjson::Document document_;
  rapidjson::SchemaValidator validator_;

  std::vector<std::string> error_messages_ = {};
};

}  // namespace broker_service

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_JSON_READER_H_
