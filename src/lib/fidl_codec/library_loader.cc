// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/library_loader.h"

#include <fstream>
#include <ios>
#include <set>
#include <sstream>

#include <rapidjson/error/en.h>

#include "src/lib/fidl_codec/builtin_semantic.h"
#include "src/lib/fidl_codec/logger.h"
#include "src/lib/fidl_codec/semantic_parser.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

// See library_loader.h for details.

namespace fidl_codec {

EnumOrBits::EnumOrBits(const rapidjson::Value* json_definition)
    : json_definition_(json_definition) {}

EnumOrBits::~EnumOrBits() = default;

void EnumOrBits::DecodeTypes(bool is_scalar, const std::string& supertype_name,
                             Library* enclosing_library) {
  if (json_definition_ == nullptr) {
    return;
  }
  const rapidjson::Value* json_definition = json_definition_;
  // Reset json_definition_ member to allow recursive declarations.
  json_definition_ = nullptr;
  name_ = enclosing_library->ExtractString(json_definition, supertype_name, "<unknown>", "name");
  if (is_scalar) {
    type_ = enclosing_library->ExtractScalarType(json_definition, supertype_name, name_, "type");
  } else {
    type_ = enclosing_library->ExtractType(json_definition, supertype_name, name_, "type");
  }

  if (!json_definition->HasMember("members")) {
    enclosing_library->FieldNotFound(supertype_name, name_, "members");
  } else {
    if (json_definition->HasMember("members")) {
      for (auto& member : (*json_definition)["members"].GetArray()) {
        if (member.HasMember("value") && member["value"].HasMember("literal")) {
          if (!member.HasMember("name")) {
            continue;
          }

          const char* data = member["value"]["literal"]["value"].GetString();
          bool negative = false;
          if (*data == '-') {
            ++data;
            negative = true;
          }
          std::stringstream stream;
          stream.str(std::string(data));
          uint64_t absolute_value;
          stream >> absolute_value;
          members_.emplace_back(member["name"].GetString(), absolute_value, negative);
        }
      }
    }
  }

  size_v2_ = type_->InlineSize(WireVersion::kWireV2);
}

Enum::~Enum() = default;

std::string Enum::GetName(uint64_t absolute_value, bool negative) const {
  for (auto& member : members()) {
    if ((member.absolute_value() == absolute_value) && (member.negative() == negative)) {
      return member.name();
    }
  }
  return "<unknown>";
}

Bits::~Bits() = default;

std::string Bits::GetName(uint64_t absolute_value, bool negative) const {
  std::ostringstream os;
  auto emit = [&os, first = true]() mutable -> std::ostringstream& {
    if (!first) {
      os << '|';
    }
    first = false;
    return os;
  };
  if (!negative) {
    for (const auto& member : members()) {
      if (member.negative()) {
        continue;
      }
      if (absolute_value & member.absolute_value()) {
        emit() << member.name();
      }
      absolute_value &= ~member.absolute_value();
    }
    if (absolute_value) {
      emit() << std::hex << std::showbase << absolute_value;
    }
  }

  std::string str = os.str();
  if (!str.empty()) {
    return str;
  }
  return "<none>";
}

Parameter::Parameter(std::string name, Type* type) : name_(std::move(name)), type_(type) {}

Parameter::~Parameter() = default;

Payloadable::Payloadable(Library* enclosing_library, const rapidjson::Value* json_definition,
                         std::string name)
    : enclosing_library_(enclosing_library),
      json_definition_(json_definition),
      name_(std::move(name)) {}

Payloadable::~Payloadable() = default;

Payload::Payload(Library* enclosing_library, const ProtocolMethod* method,
                 const rapidjson::Value* json_type_definition, Payloadable* payloadable)
    : enclosing_library_(enclosing_library),
      enclosing_method_(method),
      type_definition_(json_type_definition),
      payloadable_(payloadable) {}

Payload::~Payload() = default;

void Payload::DecodeTypes() {
  if (type_ == nullptr) {
    type_ = Type::GetType(enclosing_library_->enclosing_loader(), *type_definition_);
    payloadable_->DecodeTypes();
  }
}

std::unique_ptr<PayloadableValue> Payload::Decode(MessageDecoder& decoder) const {
  return payloadable_->DecodeAsPayload(type_, decoder);
}

std::unique_ptr<Parameter> Payload::FindParameter(std::string_view name) {
  return payloadable_->FindParameter(name, type_);
}

Struct* Payload::AsStruct() {
  if (type_->AsStructType() != nullptr) {
    return static_cast<Struct*>(payloadable_);
  }
  return nullptr;
}

const Struct* Payload::AsStruct() const {
  if (type_->AsStructType() != nullptr) {
    return static_cast<const Struct*>(payloadable_);
  }
  return nullptr;
}

Table* Payload::AsTable() {
  if (type_->AsTableType() != nullptr) {
    return static_cast<Table*>(payloadable_);
  }
  return nullptr;
}

const Table* Payload::AsTable() const {
  if (type_->AsTableType() != nullptr) {
    return static_cast<const Table*>(payloadable_);
  }
  return nullptr;
}

Union* Payload::AsUnion() {
  if (type_->AsUnionType() != nullptr) {
    return static_cast<Union*>(payloadable_);
  }
  return nullptr;
}

const Union* Payload::AsUnion() const {
  if (type_->AsUnionType() != nullptr) {
    return static_cast<const Union*>(payloadable_);
  }
  return nullptr;
}

std::string Payload::ToString(bool expand) const { return payloadable_->ToString(expand); }

UnionMember::UnionMember(const Union& union_definition, Library* enclosing_library,
                         const rapidjson::Value* json_definition)
    : union_definition_(union_definition),
      reserved_(
          enclosing_library->ExtractBool(json_definition, "table member", "<unknown>", "reserved")),
      name_(reserved_ ? "<reserved>"
                      : enclosing_library->ExtractString(json_definition, "union member",
                                                         "<unknown>", "name")),
      ordinal_(enclosing_library->ExtractUint64(json_definition, "union member", name_, "ordinal")),
      type_(reserved_
                ? std::make_unique<InvalidType>()
                : enclosing_library->ExtractType(json_definition, "union member", name_, "type")) {}

UnionMember::~UnionMember() = default;

Union::Union(Library* enclosing_library, const rapidjson::Value* json_definition)
    : Payloadable(enclosing_library, json_definition, "") {}

Union::~Union() = default;

std::unique_ptr<PayloadableValue> Union::DecodeAsPayload(const std::unique_ptr<Type>& payload_type,
                                                         MessageDecoder& decoder) const {
  decoder.SkipObject(payload_type->InlineSize(decoder.version()));
  std::unique_ptr<Value> value = payload_type->Decode(&decoder, kTransactionHeaderSize);
  UnionValue* struct_value = static_cast<UnionValue*>(value.release());
  return std::unique_ptr<PayloadableValue>(struct_value);
}

void Union::DecodeTypes() {
  if (json_definition_ == nullptr) {
    return;
  }
  const rapidjson::Value* json_definition = json_definition_;
  // Reset json_definition_ member to allow recursive declarations.
  json_definition_ = nullptr;
  name_ = enclosing_library_->ExtractString(json_definition, "union", "<unknown>", "name");

  if (!json_definition->HasMember("members")) {
    enclosing_library_->FieldNotFound("union", name_, "members");
  } else {
    auto member_arr = (*json_definition)["members"].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.push_back(std::make_unique<UnionMember>(*this, enclosing_library_, &member));
    }
  }
}

std::unique_ptr<Parameter> Union::FindParameter(std::string_view _,
                                                const std::unique_ptr<Type>& payload_type) {
  return std::make_unique<Parameter>("payload", payload_type.get());
}

const UnionMember* Union::MemberFromOrdinal(Ordinal64 ordinal) const {
  for (const auto& member : members_) {
    if (member->ordinal() == ordinal) {
      if (member->reserved()) {
        return nullptr;
      }
      return member.get();
    }
  }
  return nullptr;
}

UnionMember* Union::SearchMember(std::string_view name) const {
  for (const auto& member : members_) {
    if (member->name() == name) {
      return member.get();
    }
  }
  return nullptr;
}

std::string Union::ToString(bool expand) const {
  UnionType type(*this, false);
  return type.ToString(expand);
}

StructMember::StructMember(Library* enclosing_library, const rapidjson::Value* json_definition)
    : name_(
          enclosing_library->ExtractString(json_definition, "struct member", "<unknown>", "name")),
      offset_v1_(enclosing_library->ExtractFieldOffset(json_definition, "struct member", name_,
                                                       "field_shape_v1")),
      offset_v2_(enclosing_library->ExtractFieldOffset(json_definition, "struct member", name_,
                                                       "field_shape_v2")),
      type_(enclosing_library->ExtractType(json_definition, "struct member", name_, "type")) {}

StructMember::StructMember(std::string_view name, std::unique_ptr<Type> type)
    : name_(name), type_(std::move(type)) {}

StructMember::StructMember(std::string_view name, std::unique_ptr<Type> type, uint8_t id)
    : name_(name), type_(std::move(type)), id_(id) {}

StructMember::~StructMember() = default;

void StructMember::reset_type() { type_ = nullptr; }

const Struct Struct::Empty = Struct();

Struct::Struct(Library* enclosing_library, const rapidjson::Value* json_definition)
    : Payloadable(enclosing_library, json_definition, "") {}

Struct::Struct(std::string_view name) : Payloadable(nullptr, nullptr, std::string(name)) {}

Struct::~Struct() = default;

void Struct::AddMember(std::string_view name, std::unique_ptr<Type> type, uint32_t id) {
  members_.emplace_back(std::make_unique<StructMember>(name, std::move(type), id));
}

void Struct::DecodeTypes() {
  if (json_definition_ == nullptr) {
    return;
  }
  const rapidjson::Value* json_definition = json_definition_;
  // Reset json_definition_ member to allow recursive declarations.
  json_definition_ = nullptr;
  name_ = enclosing_library_->ExtractString(json_definition, "struct", "<unknown>", "name");

  if (!json_definition->HasMember("type_shape_v2")) {
    enclosing_library_->FieldNotFound("struct", name_, "type_shape_v2");
  } else {
    const rapidjson::Value& v2 = (*json_definition)["type_shape_v2"];
    size_v2_ = static_cast<uint32_t>(
        enclosing_library_->ExtractUint64(&v2, "struct", name_, "inline_size"));
  }

  if (!json_definition->HasMember("members")) {
    enclosing_library_->FieldNotFound("struct", name_, "members");
  } else {
    auto member_arr = (*json_definition)["members"].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.push_back(std::make_unique<StructMember>(enclosing_library_, &member));
    }
  }
}

std::unique_ptr<PayloadableValue> Struct::DecodeAsPayload(const std::unique_ptr<Type>& payload_type,
                                                          MessageDecoder& decoder) const {
  decoder.SkipObject(payload_type->InlineSize(decoder.version()));
  std::unique_ptr<Value> value = payload_type->Decode(&decoder, kTransactionHeaderSize);
  StructValue* struct_value = static_cast<StructValue*>(value.release());
  return std::unique_ptr<PayloadableValue>(struct_value);
}

std::unique_ptr<Parameter> Struct::FindParameter(std::string_view name,
                                                 const std::unique_ptr<Type>& _) {
  StructMember* found = this->SearchMember(name, 0);
  return found != nullptr ? std::make_unique<Parameter>(found->name(), found->type()) : nullptr;
}

StructMember* Struct::SearchMember(std::string_view name, uint32_t id) const {
  for (const auto& member : members_) {
    if (member->name() == name && member->id() == id) {
      return member.get();
    }
  }
  return nullptr;
}

uint32_t Struct::Size(WireVersion version) const { return size_v2_; }

std::string Struct::ToString(bool expand) const {
  StructType type(*this, false);
  return type.ToString(expand);
}

void Struct::VisitAsType(TypeVisitor* visitor) const {
  StructType type(*this, false);
  type.Visit(visitor);
}

TableMember::TableMember(Library* enclosing_library, const rapidjson::Value* json_definition)
    : reserved_(
          enclosing_library->ExtractBool(json_definition, "table member", "<unknown>", "reserved")),
      name_(reserved_ ? "<reserved>"
                      : enclosing_library->ExtractString(json_definition, "table member",
                                                         "<unknown>", "name")),
      ordinal_(enclosing_library->ExtractUint32(json_definition, "table member", name_, "ordinal")),
      type_(reserved_
                ? std::make_unique<InvalidType>()
                : enclosing_library->ExtractType(json_definition, "table member", name_, "type")) {}

TableMember::~TableMember() = default;

Table::Table(Library* enclosing_library, const rapidjson::Value* json_definition)
    : Payloadable(enclosing_library, json_definition, "") {}

Table::~Table() = default;

std::unique_ptr<PayloadableValue> Table::DecodeAsPayload(const std::unique_ptr<Type>& payload_type,
                                                         MessageDecoder& decoder) const {
  decoder.SkipObject(payload_type->InlineSize(decoder.version()));
  std::unique_ptr<Value> value = payload_type->Decode(&decoder, kTransactionHeaderSize);
  TableValue* struct_value = static_cast<TableValue*>(value.release());
  return std::unique_ptr<PayloadableValue>(struct_value);
}

void Table::DecodeTypes() {
  if (json_definition_ == nullptr) {
    return;
  }
  const rapidjson::Value* json_definition = json_definition_;
  // Reset json_definition_ member to allow recursive declarations.
  json_definition_ = nullptr;
  name_ = enclosing_library_->ExtractString(json_definition, "table", "<unknown>", "name");

  if (!json_definition->HasMember("members")) {
    enclosing_library_->FieldNotFound("table", name_, "members");
  } else {
    auto member_arr = (*json_definition)["members"].GetArray();
    for (auto& member : member_arr) {
      auto table_member = std::make_unique<TableMember>(enclosing_library_, &member);
      Ordinal32 ordinal = table_member->ordinal();
      if (ordinal >= members_.size()) {
        members_.resize(ordinal + 1);
      }
      members_[ordinal] = std::move(table_member);
    }
  }
}

std::unique_ptr<Parameter> Table::FindParameter(std::string_view _,
                                                const std::unique_ptr<Type>& payload_type) {
  return std::make_unique<Parameter>("payload", payload_type.get());
}

const TableMember* Table::MemberFromOrdinal(Ordinal64 ordinal) const {
  if (ordinal >= members_.size()) {
    return nullptr;
  }
  return members_[ordinal].get();
}

const TableMember* Table::SearchMember(std::string_view name) const {
  for (const auto& member : members_) {
    if ((member != nullptr) && (member->name() == name)) {
      return member.get();
    }
  }
  return nullptr;
}

std::string Table::ToString(bool expand) const {
  TableType type(*this);
  return type.ToString(expand);
}

ProtocolMethod::ProtocolMethod(Library* enclosing_library, const Protocol& protocol,
                               const rapidjson::Value* json_definition)
    : enclosing_library_(enclosing_library),
      enclosing_protocol_(&protocol),
      name_(protocol.enclosing_library()->ExtractString(json_definition, "method", "<unknown>",
                                                        "name")),
      ordinal_(
          protocol.enclosing_library()->ExtractUint64(json_definition, "method", name_, "ordinal")),
      is_composed_(protocol.enclosing_library()->ExtractBool(json_definition, "method", name_,
                                                             "is_composed")),
      has_request_(protocol.enclosing_library()->ExtractBool(json_definition, "method", name_,
                                                             "has_request")),
      has_response_(protocol.enclosing_library()->ExtractBool(json_definition, "method", name_,
                                                              "has_response")) {
  if (protocol.enclosing_library()->ExtractBool(json_definition, "method", name_, "has_request")) {
    if (json_definition->HasMember("maybe_request_payload")) {
      const rapidjson::Value& payload_type = (*json_definition)["maybe_request_payload"];
      if (!payload_type.HasMember("identifier")) {
        enclosing_library_->FieldNotFound("request", name_, "identifier");
      } else {
        auto payload_name = payload_type["identifier"].GetString();
        auto payloadable = enclosing_library_->GetPayloadable(payload_name);
        request_ = std::make_unique<Payload>(
            enclosing_library, this, &(*json_definition)["maybe_request_payload"], payloadable);
      }
    }
  }

  if (protocol.enclosing_library()->ExtractBool(json_definition, "method", name_, "has_response")) {
    if (json_definition->HasMember("maybe_response_payload")) {
      const rapidjson::Value& payload_type = (*json_definition)["maybe_response_payload"];
      if (!payload_type.HasMember("identifier")) {
        enclosing_library_->FieldNotFound("response", name_, "identifier");
      } else {
        auto payload_name = payload_type["identifier"].GetString();
        auto payloadable = enclosing_library_->GetPayloadable(payload_name);
        response_ = std::make_unique<Payload>(
            enclosing_library, this, &(*json_definition)["maybe_response_payload"], payloadable);
      }
    }
  }
}

ProtocolMethod::~ProtocolMethod() = default;

std::string ProtocolMethod::fully_qualified_name() const {
  std::string fqn(enclosing_protocol_->name());
  fqn.append(".");
  fqn.append(name());
  return fqn;
}

std::unique_ptr<Parameter> ProtocolMethod::FindParameter(std::string_view name) const {
  if (request_ != nullptr) {
    std::unique_ptr<Parameter> param = request_->FindParameter(name);
    if (param != nullptr) {
      return param;
    }
  }
  if (response_ != nullptr) {
    return response_->FindParameter(name);
  }
  return nullptr;
}

void Protocol::AddMethodsToIndex(LibraryLoader* library_loader) {
  for (auto& protocol_method : protocol_methods_) {
    library_loader->AddMethod(protocol_method.get());
  }
}

bool Protocol::GetMethodByFullName(const std::string& name,
                                   const ProtocolMethod** method_ptr) const {
  for (const auto& method : methods()) {
    if (method->fully_qualified_name() == name) {
      *method_ptr = method.get();
      return true;
    }
  }
  return false;
}

ProtocolMethod* Protocol::GetMethodByName(std::string_view name) const {
  for (const auto& method : methods()) {
    if (method->name() == name) {
      return method.get();
    }
  }
  return nullptr;
}

Library::Library(LibraryLoader* enclosing_loader, rapidjson::Document& json_definition)
    : enclosing_loader_(enclosing_loader), json_definition_(std::move(json_definition)) {
  if (!json_definition_.HasMember("struct_declarations")) {
    FieldNotFound("library", name_, "struct_declarations");
  } else if (!json_definition_.HasMember("external_struct_declarations")) {
    FieldNotFound("library", name_, "external_struct_declarations");
  } else if (!json_definition_.HasMember("table_declarations")) {
    FieldNotFound("library", name_, "table_declarations");
  } else if (!json_definition_.HasMember("union_declarations")) {
    FieldNotFound("library", name_, "union_declarations");
  } else {
    // Make a set of the encoded compound identifiers of all types that are used as payloads for
    // for FIDL methods.
    std::set<std::string> message_body_type_names;
    for (auto& protocol : json_definition_["protocol_declarations"].GetArray()) {
      for (auto& method : protocol["methods"].GetArray()) {
        if (method.HasMember("maybe_request_payload")) {
          message_body_type_names.insert(method["maybe_request_payload"]["identifier"].GetString());
        }
        if (method.HasMember("maybe_response_payload")) {
          message_body_type_names.insert(
              method["maybe_response_payload"]["identifier"].GetString());
        }
      }
    }

    // Add all type declarations used as payloads to the payload store.
    for (auto& str : json_definition_["struct_declarations"].GetArray()) {
      const std::string struct_name = str["name"].GetString();
      if (message_body_type_names.find(struct_name) != message_body_type_names.end()) {
        payloadables_.emplace(std::piecewise_construct, std::forward_as_tuple(struct_name),
                              std::forward_as_tuple(new Struct(this, &str)));
      }
    }
    for (auto& str : json_definition_["external_struct_declarations"].GetArray()) {
      const std::string struct_name = str["name"].GetString();
      if (message_body_type_names.find(struct_name) != message_body_type_names.end()) {
        payloadables_.emplace(std::piecewise_construct, std::forward_as_tuple(struct_name),
                              std::forward_as_tuple(new Struct(this, &str)));
      }
    }
    for (auto& tab : json_definition_["table_declarations"].GetArray()) {
      const std::string tab_name = tab["name"].GetString();
      if (message_body_type_names.find(tab_name) != message_body_type_names.end()) {
        payloadables_.emplace(std::piecewise_construct, std::forward_as_tuple(tab_name),
                              std::forward_as_tuple(new Table(this, &tab)));
      }
    }
    for (auto& uni : json_definition_["union_declarations"].GetArray()) {
      const std::string uni_name = uni["name"].GetString();
      if (message_body_type_names.find(uni_name) != message_body_type_names.end()) {
        payloadables_.emplace(std::piecewise_construct, std::forward_as_tuple(uni_name),
                              std::forward_as_tuple(new Union(this, &uni)));
      }
    }
  }

  auto protocols_array = json_definition_["protocol_declarations"].GetArray();
  protocols_.reserve(protocols_array.Size());

  for (auto& decl : protocols_array) {
    protocols_.emplace_back(new Protocol(this, decl));
    protocols_.back()->AddMethodsToIndex(enclosing_loader);
  }
}

Library::~Library() { enclosing_loader()->Delete(this); }

void Library::DecodeTypes() {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = ExtractString(&json_definition_, "library", "<unknown>", "name");

  if (!json_definition_.HasMember("enum_declarations")) {
    FieldNotFound("library", name_, "enum_declarations");
  } else {
    for (auto& enu : json_definition_["enum_declarations"].GetArray()) {
      enums_.emplace(std::piecewise_construct, std::forward_as_tuple(enu["name"].GetString()),
                     std::forward_as_tuple(new Enum(&enu)));
    }
  }

  if (!json_definition_.HasMember("bits_declarations")) {
    FieldNotFound("library", name_, "bits_declarations");
  } else {
    for (auto& bits : json_definition_["bits_declarations"].GetArray()) {
      bits_.emplace(std::piecewise_construct, std::forward_as_tuple(bits["name"].GetString()),
                    std::forward_as_tuple(new Bits(&bits)));
    }
  }

  if (!json_definition_.HasMember("struct_declarations")) {
    FieldNotFound("library", name_, "struct_declarations");
  } else {
    for (auto& str : json_definition_["struct_declarations"].GetArray()) {
      structs_.emplace(std::piecewise_construct, std::forward_as_tuple(str["name"].GetString()),
                       std::forward_as_tuple(new Struct(this, &str)));
    }
  }

  if (!json_definition_.HasMember("external_struct_declarations")) {
    FieldNotFound("library", name_, "external_struct_declarations");
  } else {
    for (auto& str : json_definition_["external_struct_declarations"].GetArray()) {
      structs_.emplace(std::piecewise_construct, std::forward_as_tuple(str["name"].GetString()),
                       std::forward_as_tuple(new Struct(this, &str)));
    }
  }

  if (!json_definition_.HasMember("table_declarations")) {
    FieldNotFound("library", name_, "table_declarations");
  } else {
    for (auto& tab : json_definition_["table_declarations"].GetArray()) {
      tables_.emplace(std::piecewise_construct, std::forward_as_tuple(tab["name"].GetString()),
                      std::forward_as_tuple(new Table(this, &tab)));
    }
  }

  if (!json_definition_.HasMember("union_declarations")) {
    FieldNotFound("library", name_, "union_declarations");
  } else {
    for (auto& uni : json_definition_["union_declarations"].GetArray()) {
      unions_.emplace(std::piecewise_construct, std::forward_as_tuple(uni["name"].GetString()),
                      std::forward_as_tuple(new Union(this, &uni)));
    }
  }
}

bool Library::DecodeAll() {
  DecodeTypes();
  for (const auto& tmp : structs_) {
    tmp.second->DecodeTypes();
  }
  for (const auto& tmp : enums_) {
    tmp.second->DecodeTypes(this);
  }
  for (const auto& tmp : bits_) {
    tmp.second->DecodeTypes(this);
  }
  for (const auto& tmp : tables_) {
    tmp.second->DecodeTypes();
  }
  for (const auto& tmp : unions_) {
    tmp.second->DecodeTypes();
  }
  for (const auto& protocol : protocols_) {
    for (const auto& method : protocol->methods()) {
      method->DecodeTypes();
    }
  }
  return !has_errors_;
}

std::unique_ptr<Type> Library::TypeFromIdentifier(bool is_nullable, const std::string& identifier) {
  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    str->second->DecodeTypes();
    std::unique_ptr<Type> type(new StructType(std::ref(*str->second), is_nullable));
    return type;
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    enu->second->DecodeTypes(this);
    return std::make_unique<EnumType>(std::ref(*enu->second));
  }
  auto bits = bits_.find(identifier);
  if (bits != bits_.end()) {
    bits->second->DecodeTypes(this);
    return std::make_unique<BitsType>(std::ref(*bits->second));
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
  Protocol* ifc;
  if (GetProtocolByName(identifier, &ifc)) {
    return std::make_unique<HandleType>();
  }
  return std::make_unique<InvalidType>();
}

bool Library::GetProtocolByName(std::string_view name, Protocol** ptr) const {
  for (const auto& protocol : protocols()) {
    if (protocol->name() == name) {
      *ptr = protocol.get();
      return true;
    }
  }
  return false;
}

bool Library::ExtractBool(const rapidjson::Value* json_definition, std::string_view container_type,
                          std::string_view container_name, const char* field_name) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return false;
  }
  return (*json_definition)[field_name].GetBool();
}

std::string Library::ExtractString(const rapidjson::Value* json_definition,
                                   std::string_view container_type, std::string_view container_name,
                                   const char* field_name) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return "<unknown>";
  }
  return (*json_definition)["name"].GetString();
}

uint64_t Library::ExtractUint64(const rapidjson::Value* json_definition,
                                std::string_view container_type, std::string_view container_name,
                                const char* field_name) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return 0;
  }
  return std::strtoll((*json_definition)[field_name].GetString(), nullptr, kDecimalBase);
}

uint32_t Library::ExtractUint32(const rapidjson::Value* json_definition,
                                std::string_view container_type, std::string_view container_name,
                                const char* field_name) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return 0;
  }
  return static_cast<uint32_t>(
      std::strtoul((*json_definition)[field_name].GetString(), nullptr, kDecimalBase));
}

std::unique_ptr<Type> Library::ExtractScalarType(const rapidjson::Value* json_definition,
                                                 std::string_view container_type,
                                                 std::string_view container_name,
                                                 const char* field_name) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return std::make_unique<InvalidType>();
  }
  return Type::ScalarTypeFromName((*json_definition)[field_name].GetString());
}

std::unique_ptr<Type> Library::ExtractType(const rapidjson::Value* json_definition,
                                           std::string_view container_type,
                                           std::string_view container_name,
                                           const char* field_name) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return std::make_unique<InvalidType>();
  }
  return Type::GetType(enclosing_loader(), (*json_definition)[field_name]);
}

uint64_t Library::ExtractFieldOffset(const rapidjson::Value* json_definition,
                                     std::string_view container_type,
                                     std::string_view container_name, const char* field_name) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return 0;
  }
  return ExtractUint64(&(*json_definition)[field_name], container_type, container_name, "offset");
}

void Library::FieldNotFound(std::string_view container_type, std::string_view container_name,
                            const char* field_name) {
  has_errors_ = true;
  FX_LOGS_OR_CAPTURE(ERROR) << "File " << name() << " field '" << field_name << "' missing for "
                            << container_type << ' ' << container_name;
}

LibraryLoader::LibraryLoader(const std::vector<std::string>& library_paths, LibraryReadError* err) {
  AddAll(library_paths, err);
}

bool LibraryLoader::AddAll(const std::vector<std::string>& library_paths, LibraryReadError* err) {
  bool ok = true;
  // Go backwards through the streams; we refuse to load the same library twice, and the last
  // one wins.
  for (auto path = library_paths.rbegin(); path != library_paths.rend(); ++path) {
    AddPath(*path, err);
    if (err->value != LibraryReadError::kOk) {
      ok = false;
    }
  }
  return ok;
}

bool LibraryLoader::DecodeAll() {
  bool ok = true;
  for (const auto& representation : representations_) {
    Library* library = representation.second.get();
    if (!library->DecodeAll()) {
      ok = false;
    }
  }
  return ok;
}

void LibraryLoader::AddPath(const std::string& path, LibraryReadError* err) {
  std::ifstream infile(path.c_str());
  std::string content(std::istreambuf_iterator<char>(infile), {});
  if (infile.fail()) {
    err->value = LibraryReadError ::kIoError;
    return;
  }
  AddContent(content, err);
  if (err->value != LibraryReadError::kOk) {
    FX_LOGS_OR_CAPTURE(ERROR) << path << ": JSON parse error: "
                              << rapidjson::GetParseError_En(err->parse_result.Code())
                              << " at offset " << err->parse_result.Offset();
    return;
  }
}

void LibraryLoader::AddContent(const std::string& content, LibraryReadError* err) {
  rapidjson::Document document;
  err->parse_result = document.Parse<rapidjson::kParseNumbersAsStringsFlag>(content.c_str());
  // TODO: This would be a good place to validate that the resulting JSON
  // matches the schema in tools/fidl/fidlc/schema.json.  If there are
  // errors, we will currently get mysterious crashes.
  if (document.HasParseError()) {
    err->value = LibraryReadError::kParseError;
    return;
  }
  std::string library_name = document["name"].GetString();
  if (representations_.find(library_name) == representations_.end()) {
    representations_.emplace(std::piecewise_construct, std::forward_as_tuple(library_name),
                             std::forward_as_tuple(new Library(this, document)));
  }
  err->value = LibraryReadError::kOk;
}

void LibraryLoader::AddMethod(const ProtocolMethod* method) {
  if (ordinal_map_[method->ordinal()] == nullptr) {
    ordinal_map_[method->ordinal()] = std::make_unique<std::vector<const ProtocolMethod*>>();
  }
  // Ensure composed methods come after non-composed methods.  The fidl_codec
  // libraries pick the first one they find.
  if (method->is_composed()) {
    ordinal_map_[method->ordinal()]->push_back(method);
  } else {
    ordinal_map_[method->ordinal()]->insert(ordinal_map_[method->ordinal()]->begin(), method);
  }
}

void LibraryLoader::ParseBuiltinSemantic() {
  semantic::ParserErrors parser_errors;
  {
    semantic::SemanticParser parser(this, semantic::builtin_semantic_fuchsia_io, &parser_errors);
    parser.ParseSemantic();
  }
  {
    semantic::SemanticParser parser(this, semantic::builtin_semantic_fuchsia_sys, &parser_errors);
    parser.ParseSemantic();
  }
}

}  // namespace fidl_codec
