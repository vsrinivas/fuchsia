// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include <src/lib/fxl/logging.h>

#include "rapidjson/error/en.h"
#include "tools/fidlcat/lib/wire_object.h"
#include "tools/fidlcat/lib/wire_types.h"

// See library_loader.h for details.

namespace fidlcat {

Enum::Enum(const rapidjson::Value& value) : value_(value) {}

Enum::~Enum() {}

void Enum::DecodeTypes() {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = value_["name"].GetString();
  type_ = Type::ScalarTypeFromName(value_["type"].GetString(), 0);
  size_ = type_->InlineSize();
}

std::string Enum::GetNameFromBytes(const uint8_t* bytes) const {
  for (auto& member : value_["members"].GetArray()) {
    if (type_->ValueEquals(bytes, size_, member["value"]["literal"])) {
      return member["name"].GetString();
    }
  }
  return "(Unknown enum member)";
}

UnionMember::UnionMember(const Library& enclosing_library,
                         const rapidjson::Value& value)
    : name_(value["name"].GetString()),
      offset_(std::strtoll(value["offset"].GetString(), nullptr, 10)),
      size_(std::strtoll(value["size"].GetString(), nullptr, 10)),
      ordinal_(value.HasMember("ordinal")
                   ? std::strtoll(value["ordinal"].GetString(), nullptr, 10)
                   : 0) {
  if (!value.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    type_ = std::make_unique<RawType>(size_);
    return;
  }
  type_ =
      Type::GetType(enclosing_library.enclosing_loader(), value["type"], size_);
}

UnionMember::~UnionMember() {}

Union::Union(const Library& enclosing_library, const rapidjson::Value& value)
    : enclosing_library_(enclosing_library), value_(value) {}

void Union::DecodeTypes() {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = value_["name"].GetString();
  alignment_ = std::strtoll(value_["alignment"].GetString(), nullptr, 10);
  size_ = std::strtoll(value_["size"].GetString(), nullptr, 10);
  auto member_arr = value_["members"].GetArray();
  members_.reserve(member_arr.Size());
  for (auto& member : member_arr) {
    members_.push_back(
        std::make_unique<UnionMember>(enclosing_library_, member));
  }
}

const UnionMember* Union::MemberWithTag(uint32_t tag) const {
  if (tag >= members_.size()) {
    return nullptr;
  }
  return members_[tag].get();
}

const UnionMember* Union::MemberWithOrdinal(Ordinal ordinal) const {
  for (const auto& member : members_) {
    if (member->ordinal() == ordinal) {
      return member.get();
    }
  }
  return nullptr;
}

std::unique_ptr<UnionField> Union::DecodeUnion(MessageDecoder* decoder,
                                               std::string_view name,
                                               uint64_t offset,
                                               bool nullable) const {
  std::unique_ptr<UnionField> result =
      std::make_unique<UnionField>(name, *this);
  if (nullable) {
    result->DecodeNullable(decoder, offset);
  } else {
    result->DecodeAt(decoder, offset);
  }
  return result;
}

StructMember::StructMember(const Library& enclosing_library,
                           const rapidjson::Value& value)
    : name_(value["name"].GetString()),
      offset_(std::strtoll(value["offset"].GetString(), nullptr, 10)),
      size_(std::strtoll(value["size"].GetString(), nullptr, 10)) {
  if (!value.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    type_ = std::make_unique<RawType>(size());
    return;
  }
  type_ =
      Type::GetType(enclosing_library.enclosing_loader(), value["type"], size_);
}

StructMember::~StructMember() {}

Struct::Struct(const Library& enclosing_library, const rapidjson::Value& value)
    : enclosing_library_(enclosing_library), value_(value) {}

void Struct::DecodeStructTypes() {
  if (decoded_) {
    return;
  }
  DecodeTypes("size", "members");
}

void Struct::DecodeRequestTypes() {
  if (decoded_) {
    return;
  }
  DecodeTypes("maybe_request_size", "maybe_request");
}

void Struct::DecodeResponseTypes() {
  if (decoded_) {
    return;
  }
  DecodeTypes("maybe_response_size", "maybe_response");
}

std::unique_ptr<Object> Struct::DecodeObject(MessageDecoder* decoder,
                                             std::string_view name,
                                             uint64_t offset,
                                             bool nullable) const {
  std::unique_ptr<Object> result = std::make_unique<Object>(name, *this);
  if (nullable) {
    result->DecodeNullable(decoder, offset);
  } else {
    result->DecodeAt(decoder, offset);
  }
  return result;
}

void Struct::DecodeTypes(std::string size_name, std::string member_name) {
  FXL_DCHECK(!decoded_);
  decoded_ = true;
  name_ = value_["name"].GetString();
  size_ = std::strtoll(value_[size_name].GetString(), nullptr, 10);
  auto member_arr = value_[member_name].GetArray();
  members_.reserve(member_arr.Size());
  for (auto& member : member_arr) {
    members_.push_back(
        std::make_unique<StructMember>(enclosing_library_, member));
  }
}

TableMember::TableMember(const Library& enclosing_library,
                         const rapidjson::Value& value)
    : name_(value["name"].GetString()),
      ordinal_(std::strtoll(value["ordinal"].GetString(), nullptr, 10)),
      size_(std::strtoll(value["size"].GetString(), nullptr, 10)) {
  if (!value.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    type_ = std::make_unique<RawType>(size_);
    return;
  }
  type_ =
      Type::GetType(enclosing_library.enclosing_loader(), value["type"], size_);
}

TableMember::~TableMember() {}

Table::Table(const Library& enclosing_library, const rapidjson::Value& value)
    : enclosing_library_(enclosing_library), value_(value) {}

Table::~Table() {}

void Table::DecodeTypes() {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = value_["name"].GetString();
  size_ = std::strtoll(value_["size"].GetString(), nullptr, 10);
  unknown_member_type_ = std::make_unique<RawType>(size_);
  auto member_arr = value_["members"].GetArray();
  uint32_t max_ordinal = 0;
  for (auto& member : member_arr) {
    backing_members_.push_back(
        std::make_unique<TableMember>(enclosing_library_, member));
    max_ordinal = std::max(max_ordinal, backing_members_.back()->ordinal());
  }

  // There is one element in this array for each possible ordinal value.  The
  // array is dense, so there are unlikely to be gaps.
  members_.resize(max_ordinal + 1, nullptr);
  for (const auto& backing_member : backing_members_) {
    members_[backing_member->ordinal()] = backing_member.get();
  }
}

InterfaceMethod::InterfaceMethod(const Interface& interface,
                                 const rapidjson::Value& value)
    : enclosing_interface_(interface),
      value_(value),
      ordinal_(std::strtoll(value["ordinal"].GetString(), nullptr, 10)),
      name_(value["name"].GetString()) {
  if (value_["has_request"].GetBool()) {
    request_ = std::unique_ptr<Struct>(
        new Struct(interface.enclosing_library(), value));
  }

  if (value_["has_response"].GetBool()) {
    response_ = std::unique_ptr<Struct>(
        new Struct(interface.enclosing_library(), value));
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
    if (method->fully_qualified_name() == name) {
      *method_ptr = method.get();
      return true;
    }
  }
  return false;
}

Library::Library(LibraryLoader* enclosing_loader, rapidjson::Document& document,
                 std::map<Ordinal, const InterfaceMethod*>& index)
    : enclosing_loader_(enclosing_loader),
      backing_document_(std::move(document)) {
  auto interfaces_array =
      backing_document_["interface_declarations"].GetArray();
  interfaces_.reserve(interfaces_array.Size());

  for (auto& decl : interfaces_array) {
    interfaces_.emplace_back(new Interface(*this, decl));
    interfaces_.back()->AddMethodsToIndex(index);
  }
}

void Library::DecodeTypes() {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = backing_document_["name"].GetString();
  for (auto& enu : backing_document_["enum_declarations"].GetArray()) {
    enums_.emplace(std::piecewise_construct,
                   std::forward_as_tuple(enu["name"].GetString()),
                   std::forward_as_tuple(new Enum(enu)));
  }
  for (auto& str : backing_document_["struct_declarations"].GetArray()) {
    structs_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(str["name"].GetString()),
                     std::forward_as_tuple(new Struct(*this, str)));
  }
  for (auto& tab : backing_document_["table_declarations"].GetArray()) {
    tables_.emplace(std::piecewise_construct,
                    std::forward_as_tuple(tab["name"].GetString()),
                    std::forward_as_tuple(new Table(*this, tab)));
  }
  for (auto& uni : backing_document_["union_declarations"].GetArray()) {
    unions_.emplace(std::piecewise_construct,
                    std::forward_as_tuple(uni["name"].GetString()),
                    std::forward_as_tuple(new Union(*this, uni)));
  }
  for (auto& xuni : backing_document_["xunion_declarations"].GetArray()) {
    xunions_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(xuni["name"].GetString()),
                     std::forward_as_tuple(new XUnion(*this, xuni)));
  }
}

std::unique_ptr<Type> Library::TypeFromIdentifier(bool is_nullable,
                                                  std::string& identifier,
                                                  size_t inline_size) {
  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    str->second->DecodeStructTypes();
    std::unique_ptr<Type> type(
        new StructType(std::ref(*str->second), is_nullable));
    return type;
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    enu->second->DecodeTypes();
    return std::make_unique<EnumType>(std::ref(*enu->second));
  }
  auto tab = tables_.find(identifier);
  if (tab != tables_.end()) {
    tab->second->DecodeTypes();
    return std::make_unique<TableType>(std::ref(*tab->second));
  }
  auto uni = unions_.find(identifier);
  if (uni != unions_.end()) {
    uni->second->DecodeTypes();
    return std::make_unique<UnionType>(std::ref(*uni->second), is_nullable);
  }
  auto xuni = xunions_.find(identifier);
  if (xuni != xunions_.end()) {
    // Note: XUnion and nullable XUnion are encoded in the same way
    xuni->second->DecodeTypes();
    return std::make_unique<XUnionType>(std::ref(*xuni->second), is_nullable);
  }
  return std::make_unique<RawType>(inline_size);
}

bool Library::GetInterfaceByName(const std::string& name,
                                 const Interface** ptr) const {
  for (const auto& interface : interfaces()) {
    if (interface->name() == name) {
      *ptr = interface.get();
      return true;
    }
  }
  return false;
}

LibraryLoader::LibraryLoader(
    std::vector<std::unique_ptr<std::istream>>& library_streams,
    LibraryReadError* err) {
  err->value = LibraryReadError::kOk;
  for (size_t i = 0; i < library_streams.size(); i++) {
    std::string ir(std::istreambuf_iterator<char>(*library_streams[i]), {});
    if (library_streams[i]->fail()) {
      err->value = LibraryReadError ::kIoError;
      return;
    }
    Add(ir, err);
    if (err->value != LibraryReadError::kOk) {
      FXL_LOG(ERROR) << "JSON parse error: "
                     << rapidjson::GetParseError_En(err->parse_result.Code())
                     << " at offset " << err->parse_result.Offset();
      return;
    }
  }
}

}  // namespace fidlcat
