// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_RAPIDJSON_RAPIDJSON_H_
#define APPS_MODULAR_LIB_RAPIDJSON_RAPIDJSON_H_

// rapidjson needs these defines to support C++11 features. These features
// are intentionally not autodetected by rapidjson. Therefore, this include
// file should be used for the basic rapidjson files. Other files can be
// included from //third_party/rapidjson.

#define RAPIDJSON_HAS_STDSTRING 1
#define RAPIDJSON_HAS_CXX11_RANGE_FOR 1
#define RAPIDJSON_HAS_CXX11_RVALUE_REFS 1
#define RAPIDJSON_HAS_CXX11_TYPETRAITS 1
#define RAPIDJSON_HAS_CXX11_NOEXCEPT 1

#include <sstream>
#include <string>

#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/pointer.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace modular {

using JsonDoc = rapidjson::Document;
using JsonValue = JsonDoc::ValueType;
using JsonPointer = rapidjson::GenericPointer<JsonValue>;

// Helper function to convert the given JsonValue to a string. Note that a
// JsonDoc is also a JsonValue.
template <typename T>
inline std::string JsonValueToString(const T& v) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  v.Accept(writer);
  return buffer.GetString();
}

inline void EscapeJsonPathElementToStream(
    std::ostringstream& oss,
    const std::string& source) {
  for (const auto c : source) {
    switch (c) {
      case '~':
        oss << "~0";
        break;
      case '/':
        oss << "~1";
        break;
      default:
        oss << c;
        break;
    }
  }
}

// Helper function to take the path segments, escape them, and create a path
// that's compliant for JSON Pointer that can be used with Set() and Update()
// in the Link. Sample usage:
//
//    std::vector<std::string> segments{"init",
//                                      "http://schema.org/Person",
//                                      "given_name"};
//    module1_link_->Set(
//        modular::EscapeJsonPath(segments.begin(), segments.end()),
//        "Frank");
//    }
template <typename T>
inline std::string EscapeJsonPath(T begin, T end) {
  std::ostringstream oss;
  for (auto itr = begin; itr != end; ++itr) {
    oss << "/";
    EscapeJsonPathElementToStream(oss, *itr);
  }
  return oss.str();
}

}  // namespace modular

#endif  // APPS_MODULAR_LIB_RAPIDJSON_RAPIDJSON_H_
