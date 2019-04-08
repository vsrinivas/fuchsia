// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include <src/lib/fxl/logging.h>

#include "rapidjson/error/en.h"
#include "tools/fidlcat/lib/wire_types.h"

// See library_loader.h for details.

namespace fidlcat {

std::unique_ptr<Type> InterfaceMethodParameter::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(enclosing_method_.enclosing_interface()
                           .enclosing_library()
                           .enclosing_loader(),
                       type);
}

std::unique_ptr<Type> Library::TypeFromIdentifier(
    bool is_nullable, std::string& identifier) const {
  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    return std::make_unique<StructType>(std::ref(str->second), is_nullable);
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    return std::make_unique<EnumType>(std::ref(enu->second));
  }
  // And probably for unions and tables.
  return Type::get_illegal();
}

const std::unique_ptr<Type> Enum::GetType() const {
  // TODO Consider caching this.
  return Type::ScalarTypeFromName(value_["type"].GetString());
}

std::string Enum::GetNameFromBytes(const uint8_t* bytes, size_t length) const {
  std::unique_ptr<Type> type = GetType();
  for (auto& member : value_["members"].GetArray()) {
    Marker end(bytes + length, nullptr);
    Marker marker(bytes, nullptr, end);
    if (type->ValueEquals(marker, length, member["value"]["literal"])) {
      return member["name"].GetString();
    }
  }
  return "(Unknown enum member)";
}

uint32_t Enum::size() const { return GetType()->InlineSize(); }

std::unique_ptr<Type> StructMember::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    // TODO: something else here.
    // Probably print out raw bytes instead.
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(
      enclosing_struct().enclosing_library().enclosing_loader(), type);
}

LibraryLoader::LibraryLoader(
    std::vector<std::unique_ptr<std::istream>>& library_streams,
    LibraryReadError* err) {
  err->value = LibraryReadError ::kOk;
  for (size_t i = 0; i < library_streams.size(); i++) {
    std::string ir(std::istreambuf_iterator<char>(*library_streams[i]), {});
    if (library_streams[i]->fail()) {
      err->value = LibraryReadError ::kIoError;
      return;
    }
    Add(ir, err);
    if (err->value != LibraryReadError ::kOk) {
      FXL_LOG(ERROR) << "JSON parse error: "
                     << rapidjson::GetParseError_En(err->parse_result.Code())
                     << " at offset " << err->parse_result.Offset();
      return;
    }
  }
}

std::string InterfaceMethod::fully_qualified_name() const {
  return enclosing_interface_.name() + "." + name();
}

}  // namespace fidlcat
