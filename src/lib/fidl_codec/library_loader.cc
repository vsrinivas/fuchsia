// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/library_loader.h"

#include <fstream>

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

  size_ = type_->InlineSize();
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
  std::string returned_value;
  if (!negative) {
    for (auto& member : members()) {
      if (((absolute_value & member.absolute_value()) != 0) && !member.negative()) {
        if (!returned_value.empty()) {
          returned_value += "|";
        }
        returned_value += member.name();
      }
    }
  }

  if (returned_value.empty()) {
    return "<none>";
  }
  return returned_value;
}

UnionMember::UnionMember(const Union& union_definition, Library* enclosing_library,
                         const rapidjson::Value* json_definition)
    : union_definition_(union_definition),
      reserved_(
          enclosing_library->ExtractBool(json_definition, "table member", "<unknown>", "reserved")),
      name_(reserved_ ? "<reserved>"
                      : enclosing_library->ExtractString(json_definition, "union member",
                                                         "<unknown>", "name")),
      ordinal_(enclosing_library->ExtractUint32(json_definition, "union member", name_, "ordinal")),
      type_(reserved_
                ? std::make_unique<InvalidType>()
                : enclosing_library->ExtractType(json_definition, "union member", name_, "type")) {}

UnionMember::~UnionMember() = default;

Union::Union(Library* enclosing_library, const rapidjson::Value* json_definition)
    : enclosing_library_(enclosing_library), json_definition_(json_definition) {}

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

const UnionMember* Union::MemberWithOrdinal(Ordinal32 ordinal) const {
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

StructMember::StructMember(Library* enclosing_library, const rapidjson::Value* json_definition)
    : name_(
          enclosing_library->ExtractString(json_definition, "struct member", "<unknown>", "name")),
      offset_(enclosing_library->ExtractFieldOffset(json_definition, "struct member", name_)),
      type_(enclosing_library->ExtractType(json_definition, "struct member", name_, "type")) {}

StructMember::StructMember(std::string_view name, std::unique_ptr<Type> type)
    : name_(name), type_(std::move(type)) {}

StructMember::StructMember(std::string_view name, std::unique_ptr<Type> type, uint8_t id)
    : name_(name), type_(std::move(type)), id_(id) {}

StructMember::~StructMember() = default;

Struct::Struct(Library* enclosing_library, const rapidjson::Value* json_definition)
    : enclosing_library_(enclosing_library), json_definition_(json_definition) {}

void Struct::AddMember(std::string_view name, std::unique_ptr<Type> type, uint32_t id) {
  members_.emplace_back(std::make_unique<StructMember>(name, std::move(type), id));
}

void Struct::DecodeStructTypes() {
  if (json_definition_ == nullptr) {
    return;
  }
  DecodeTypes("struct", "size", "members", "type_shape_v1");
}

void Struct::DecodeRequestTypes() {
  if (json_definition_ == nullptr) {
    return;
  }
  DecodeTypes("request", "maybe_request_size", "maybe_request", "maybe_request_type_shape_v1");
}

void Struct::DecodeResponseTypes() {
  if (json_definition_ == nullptr) {
    return;
  }
  DecodeTypes("response", "maybe_response_size", "maybe_response", "maybe_response_type_shape_v1");
}

void Struct::DecodeTypes(std::string_view container_name, const char* size_name,
                         const char* member_name, const char* v1_name) {
  FX_DCHECK(json_definition_ != nullptr);
  const rapidjson::Value* json_definition = json_definition_;
  // Reset json_definition_ member to allow recursive declarations.
  json_definition_ = nullptr;
  name_ = enclosing_library_->ExtractString(json_definition, container_name, "<unknown>", "name");

  if (!json_definition->HasMember(v1_name)) {
    enclosing_library_->FieldNotFound(container_name, name_, v1_name);
  } else {
    const rapidjson::Value& v1 = (*json_definition)[v1_name];
    size_ = enclosing_library_->ExtractUint64(&v1, container_name, name_, "inline_size");
  }

  if (!json_definition->HasMember(member_name)) {
    enclosing_library_->FieldNotFound(container_name, name_, member_name);
  } else {
    auto member_arr = (*json_definition)[member_name].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.push_back(std::make_unique<StructMember>(enclosing_library_, &member));
    }
  }
}

void Struct::VisitAsType(TypeVisitor* visitor) const {
  StructType type(*this, false);
  type.Visit(visitor);
}

std::string Struct::ToString(bool expand) const {
  StructType type(*this, false);
  return type.ToString(expand);
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
    : enclosing_library_(enclosing_library), json_definition_(json_definition) {}

Table::~Table() = default;

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

InterfaceMethod::InterfaceMethod(const Interface& interface,
                                 const rapidjson::Value* json_definition)
    : enclosing_interface_(&interface),
      name_(interface.enclosing_library()->ExtractString(json_definition, "method", "<unknown>",
                                                         "name")),
      ordinal_(interface.enclosing_library()->ExtractUint64(json_definition, "method", name_,
                                                            "ordinal")),
      is_composed_(interface.enclosing_library()->ExtractBool(json_definition, "method", name_,
                                                              "is_composed")) {
  if (interface.enclosing_library()->ExtractBool(json_definition, "method", name_, "has_request")) {
    request_ = std::unique_ptr<Struct>(new Struct(interface.enclosing_library(), json_definition));
  }

  if (interface.enclosing_library()->ExtractBool(json_definition, "method", name_,
                                                 "has_response")) {
    response_ = std::unique_ptr<Struct>(new Struct(interface.enclosing_library(), json_definition));
  }
}

std::string InterfaceMethod::fully_qualified_name() const {
  std::string fqn(enclosing_interface_->name());
  fqn.append(".");
  fqn.append(name());
  return fqn;
}

void Interface::AddMethodsToIndex(LibraryLoader* library_loader) {
  for (size_t i = 0; i < interface_methods_.size(); i++) {
    library_loader->AddMethod(interface_methods_[i].get());
  }
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

InterfaceMethod* Interface::GetMethodByName(std::string_view name) const {
  for (const auto& method : methods()) {
    if (method->name() == name) {
      return method.get();
    }
  }
  return nullptr;
}

Library::Library(LibraryLoader* enclosing_loader, rapidjson::Document& json_definition)
    : enclosing_loader_(enclosing_loader), json_definition_(std::move(json_definition)) {
  auto interfaces_array = json_definition_["interface_declarations"].GetArray();
  interfaces_.reserve(interfaces_array.Size());

  for (auto& decl : interfaces_array) {
    interfaces_.emplace_back(new Interface(this, decl));
    interfaces_.back()->AddMethodsToIndex(enclosing_loader);
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
    tmp.second->DecodeStructTypes();
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
  for (const auto& interface : interfaces_) {
    for (const auto& method : interface->methods()) {
      method->request();
      method->response();
    }
  }
  return !has_errors_;
}

std::unique_ptr<Type> Library::TypeFromIdentifier(bool is_nullable, const std::string& identifier) {
  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    str->second->DecodeStructTypes();
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
  Interface* ifc;
  if (GetInterfaceByName(identifier, &ifc)) {
    return std::make_unique<HandleType>();
  }
  return std::make_unique<InvalidType>();
}

bool Library::GetInterfaceByName(std::string_view name, Interface** ptr) const {
  for (const auto& interface : interfaces()) {
    if (interface->name() == name) {
      *ptr = interface.get();
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
  return std::strtoll((*json_definition)[field_name].GetString(), nullptr, kDecimalBase);
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
                                     std::string_view container_name) {
  if (!json_definition->HasMember("field_shape_v1")) {
    FieldNotFound(container_type, container_name, "field_shape_v1");
    return 0;
  }
  return ExtractUint64(&(*json_definition)["field_shape_v1"], container_type, container_name,
                       "offset");
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
  // Go backwards through the streams; we refuse to load the same library twice, and the last one
  // wins.
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
  // matches the schema in zircon/tools/fidl/schema.json.  If there are
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

void LibraryLoader::AddMethod(const InterfaceMethod* method) {
  if (ordinal_map_[method->ordinal()] == nullptr) {
    ordinal_map_[method->ordinal()] = std::make_unique<std::vector<const InterfaceMethod*>>();
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
