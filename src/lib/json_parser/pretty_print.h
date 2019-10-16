// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_JSON_PARSER_PRETTY_PRINT_H_
#define SRC_LIB_JSON_PARSER_PRETTY_PRINT_H_

#include <sstream>
#include <string>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/pointer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// This contains pretty printing routines for parsed JSON values.

namespace json_parser {

using JsonDoc = ::rapidjson::Document;
using JsonValue = JsonDoc::ValueType;
using JsonPointer = ::rapidjson::GenericPointer<JsonValue>;

// Helper function to convert the given JsonValue to a string. Note that a
// JsonDoc is also a JsonValue.
template <typename T>
inline std::string JsonValueToString(const T& v) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  v.Accept(writer);
  return buffer.GetString();
}

// Like above, but pretty prints the string.
template <typename T>
inline std::string JsonValueToPrettyString(const T& v) {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  v.Accept(writer);
  return buffer.GetString();
}

// Take a vector of std::string and convert it to "/a/b/c" form for display.
// For debugging/logging purposes only
inline std::string PrettyPrintPath(const std::vector<std::string>& path) {
  std::string result;
  for (const auto& str : path) {
    result += '/' + str;
  }
  return result;
}

}  // namespace json_parser

#endif  // SRC_LIB_JSON_PARSER_PRETTY_PRINT_H_
