// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_RAPIDJSON_RAPIDJSON_H_
#define PERIDOT_LIB_RAPIDJSON_RAPIDJSON_H_

#include <sstream>
#include <string>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace modular {

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

// Take an array of strings representing the segments in a JSON Path
// and construct a rapidjson::GenericPointer() object.
template <typename Doc>
inline rapidjson::GenericPointer<typename Doc::ValueType> CreatePointer(
    const Doc& /*doc*/, std::vector<std::string>::iterator begin,
    std::vector<std::string>::iterator end) {
  rapidjson::GenericPointer<typename Doc::ValueType> pointer;
  for (auto it = begin; it != end; ++it) {
    pointer = pointer.Append(*it, nullptr);
  }
  return pointer;
}

template <typename Doc, typename Collection>
inline rapidjson::GenericPointer<typename Doc::ValueType> CreatePointer(const Doc& doc,
                                                                        const Collection& path) {
  rapidjson::GenericPointer<typename Doc::ValueType> pointer;
  for (const auto& it : path) {
    pointer = pointer.Append(it, nullptr);
  }
  return pointer;
}

}  // namespace modular

#endif  // PERIDOT_LIB_RAPIDJSON_RAPIDJSON_H_
