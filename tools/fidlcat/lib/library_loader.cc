// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include <src/lib/fxl/logging.h>

#include "rapidjson/error/en.h"
#include "tools/fidlcat/lib/wire_types.h"

// See library_loader.h for details.

namespace fidlcat {

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

std::unique_ptr<Type> UnionMember::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(enclosing_union().enclosing_library().enclosing_loader(),
                       type);
}

const UnionMember& Union::MemberWithTag(uint32_t tag) const {
  if (tag >= members_.size() || tag < 0) {
    return get_illegal_member();
  }
  return members_[tag];
}

const UnionMember& Union::get_illegal_member() const {
  class IllegalUnionMember : public UnionMember {
   public:
    IllegalUnionMember(const Union& uni) : UnionMember(uni, value_) {}

    virtual std::unique_ptr<Type> GetType() const override {
      return Type::get_illegal();
    }

    virtual uint64_t size() const override { return enclosing_union().size(); }

    virtual const std::string_view name() const override { return "unknown"; }

   private:
    const rapidjson::Value value_;
  };
  if (illegal_ == nullptr) {
    illegal_.reset(new IllegalUnionMember(*this));
  }
  return *illegal_;
}

std::unique_ptr<Type> StructMember::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(
      enclosing_struct().enclosing_library().enclosing_loader(), type);
}

std::unique_ptr<Type> TableMember::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(enclosing_table().enclosing_library().enclosing_loader(),
                       type);
}

Table::Table(const Library& enclosing_library, const rapidjson::Value& value)
    : enclosing_library_(enclosing_library), value_(value) {
  auto member_arr = value["members"].GetArray();
  uint32_t max_ordinal = 0;
  for (auto& member : member_arr) {
    Ordinal ordinal = std::strtoll(member["ordinal"].GetString(), nullptr, 10);
    backing_members_.emplace_back(*this, member);
    max_ordinal = std::max(max_ordinal, ordinal);
  }

  // There is one element in this array for each possible ordinal value.  The
  // array is dense, so there are unlikely to be gaps.
  members_.resize(max_ordinal + 1, nullptr);
  for (auto& backing_member : backing_members_) {
    members_[backing_member.ordinal()] = &backing_member;
  }
}

InterfaceMethod::InterfaceMethod(const Interface& interface,
                                 const rapidjson::Value& value)
    : enclosing_interface_(interface),
      ordinal_(std::strtoll(value["ordinal"].GetString(), nullptr, 10)),
      name_(value["name"].GetString()),
      value_(value) {
  if (value_["has_request"].GetBool()) {
    request_ = std::make_unique<Struct>(interface.enclosing_library(), value,
                                        "maybe_request_size", "maybe_request");
  }

  if (value_["has_response"].GetBool()) {
    response_ =
        std::make_unique<Struct>(interface.enclosing_library(), value,
                                 "maybe_response_size", "maybe_response");
  }
}

std::string InterfaceMethod::fully_qualified_name() const {
  std::string fqn(enclosing_interface_.name());
  fqn.append(".");
  fqn.append(name());
  return fqn;
}

bool Interface::GetMethodByFullName(const std::string& name,
                                    const InterfaceMethod** method_ptr) const {
  for (const auto& method : methods()) {
    if (method.fully_qualified_name() == name) {
      *method_ptr = &method;
      return true;
    }
  }
  return false;
}

Library::Library(const LibraryLoader& enclosing, rapidjson::Document& document)
    : enclosing_loader_(enclosing), backing_document_(std::move(document)) {
  auto interfaces_array =
      backing_document_["interface_declarations"].GetArray();
  interfaces_.reserve(interfaces_array.Size());

  for (auto& decl : interfaces_array) {
    interfaces_.emplace_back(*this, decl);
  }
  for (auto& enu : backing_document_["enum_declarations"].GetArray()) {
    enums_.emplace(std::piecewise_construct,
                   std::forward_as_tuple(enu["name"].GetString()),
                   std::forward_as_tuple(*this, enu));
  }
  for (auto& str : backing_document_["struct_declarations"].GetArray()) {
    structs_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(str["name"].GetString()),
                     std::forward_as_tuple(*this, str, "size", "members"));
  }
  for (auto& tab : backing_document_["table_declarations"].GetArray()) {
    tables_.emplace(std::piecewise_construct,
                    std::forward_as_tuple(tab["name"].GetString()),
                    std::forward_as_tuple(*this, tab));
  }
  for (auto& uni : backing_document_["union_declarations"].GetArray()) {
    unions_.emplace(std::piecewise_construct,
                    std::forward_as_tuple(uni["name"].GetString()),
                    std::forward_as_tuple(*this, uni));
  }
  for (auto& xuni : backing_document_["xunion_declarations"].GetArray()) {
    xunions_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(xuni["name"].GetString()),
                     std::forward_as_tuple(*this, xuni));
  }
}

std::unique_ptr<Type> Library::TypeFromIdentifier(
    bool is_nullable, std::string& identifier) const {
  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    std::unique_ptr<Type> type(new StructType(std::ref(str->second)));
    if (is_nullable) {
      return std::make_unique<PointerType>(type.release());
    }
    return type;
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    return std::make_unique<EnumType>(std::ref(enu->second));
  }
  auto tab = tables_.find(identifier);
  if (tab != tables_.end()) {
    std::unique_ptr<Type> type(new TableType(std::ref(tab->second)));
    return type;
  }
  auto uni = unions_.find(identifier);
  if (uni != unions_.end()) {
    std::unique_ptr<Type> type(new UnionType(std::ref(uni->second)));
    if (is_nullable) {
      return std::make_unique<PointerType>(type.release());
    }
    return type;
  }
  auto xuni = xunions_.find(identifier);
  if (xuni != xunions_.end()) {
    // Note: XUnion and nullable XUnion are encoded in the same way
    return std::make_unique<XUnionType>(std::ref(xuni->second), is_nullable);
  }
  return Type::get_illegal();
}

bool Library::GetInterfaceByName(const std::string& name,
                                 const Interface** ptr) const {
  for (const auto& interface : interfaces()) {
    if (interface.name() == name) {
      *ptr = &interface;
      return true;
    }
  }
  return false;
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

}  // namespace fidlcat
