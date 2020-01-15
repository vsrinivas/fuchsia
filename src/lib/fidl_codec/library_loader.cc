// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include "rapidjson/error/en.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "src/lib/fxl/logging.h"

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
  name_ = enclosing_library->ExtractString(json_definition_, supertype_name, "<unknown>", "name");
  if (is_scalar) {
    type_ =
        enclosing_library->ExtractScalarType(json_definition_, supertype_name, name_, "type", 0);
  } else {
    type_ = enclosing_library->ExtractType(json_definition_, supertype_name, name_, "type", 0);
  }

  if (!json_definition_->HasMember("members")) {
    enclosing_library->FieldNotFound(supertype_name, name_, "members");
  } else {
    if (json_definition_->HasMember("members")) {
      for (auto& member : (*json_definition_)["members"].GetArray()) {
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
  json_definition_ = nullptr;
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
                         const rapidjson::Value* json_definition, bool for_xunion)
    : union_definition_(union_definition),
      reserved_(
          enclosing_library->ExtractBool(json_definition, "table member", "<unknown>", "reserved")),
      name_(reserved_ ? "<reserved>"
                      : enclosing_library->ExtractString(json_definition, "union member",
                                                         "<unknown>", "name")),
      offset_(reserved_ ? 0
                        : enclosing_library->ExtractUint64(json_definition, "union member", name_,
                                                           "offset")),
      size_(reserved_
                ? 0
                : enclosing_library->ExtractUint64(json_definition, "union member", name_, "size")),
      ordinal_(for_xunion ? enclosing_library->ExtractUint32(json_definition, "union member", name_,
                                                             "ordinal")
                          : (json_definition->HasMember("xunion_ordinal")
                                 ? enclosing_library->ExtractUint32(json_definition, "union member",
                                                                    name_, "xunion_ordinal")
                                 : 0)),
      type_(reserved_ ? std::make_unique<RawType>(0)
                      : enclosing_library->ExtractType(json_definition, "union member", name_,
                                                       "type", size_)) {}

UnionMember::~UnionMember() = default;

Union::Union(Library* enclosing_library, const rapidjson::Value* json_definition)
    : enclosing_library_(enclosing_library), json_definition_(json_definition) {}

void Union::DecodeTypes(bool for_xunion) {
  if (json_definition_ == nullptr) {
    return;
  }
  name_ = enclosing_library_->ExtractString(json_definition_, "union", "<unknown>", "name");
  alignment_ = enclosing_library_->ExtractUint64(json_definition_, "union", name_, "alignment");
  size_ = enclosing_library_->ExtractUint64(json_definition_, "union", name_, "size");

  if (!json_definition_->HasMember("members")) {
    enclosing_library_->FieldNotFound("union", name_, "members");
  } else {
    auto member_arr = (*json_definition_)["members"].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.push_back(
          std::make_unique<UnionMember>(*this, enclosing_library_, &member, for_xunion));
    }
  }
  json_definition_ = nullptr;
}

const UnionMember* Union::MemberWithTag(uint32_t tag) const {
  // Only non reserved members count for the tag.
  for (const auto& member : members_) {
    if (!member->reserved()) {
      if (tag == 0) {
        return member.get();
      }
      --tag;
    }
  }
  return nullptr;
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
      size_(enclosing_library->ExtractUint64(json_definition, "struct member", name_, "size")),
      type_(
          enclosing_library->ExtractType(json_definition, "struct member", name_, "type", size_)) {
  if (!json_definition->HasMember("field_shape_v1")) {
    enclosing_library->FieldNotFound("struct member", name_, "field_shape_v1");
  } else {
    const rapidjson::Value& v1 = (*json_definition)["field_shape_v1"];
    offset_ = enclosing_library->ExtractUint64(&v1, "struct member", name_, "offset");
  }
}

StructMember::~StructMember() = default;

Struct::Struct(Library* enclosing_library, const rapidjson::Value* json_definition)
    : enclosing_library_(enclosing_library), json_definition_(json_definition) {}

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
  FXL_DCHECK(json_definition_ != nullptr);
  name_ = enclosing_library_->ExtractString(json_definition_, container_name, "<unknown>", "name");

  if (!json_definition_->HasMember(v1_name)) {
    enclosing_library_->FieldNotFound(container_name, name_, v1_name);
  } else {
    const rapidjson::Value& v1 = (*json_definition_)[v1_name];
    size_ = enclosing_library_->ExtractUint64(&v1, container_name, name_, "inline_size");
  }

  if (!json_definition_->HasMember(member_name)) {
    enclosing_library_->FieldNotFound(container_name, name_, member_name);
  } else {
    auto member_arr = (*json_definition_)[member_name].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.push_back(std::make_unique<StructMember>(enclosing_library_, &member));
    }
  }
  json_definition_ = nullptr;
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
      size_(reserved_
                ? 0
                : enclosing_library->ExtractUint64(json_definition, "table member", name_, "size")),
      type_(reserved_ ? std::make_unique<RawType>(0)
                      : enclosing_library->ExtractType(json_definition, "table member", name_,
                                                       "type", size_)) {}

TableMember::~TableMember() = default;

Table::Table(Library* enclosing_library, const rapidjson::Value* json_definition)
    : enclosing_library_(enclosing_library), json_definition_(json_definition) {}

Table::~Table() = default;

void Table::DecodeTypes() {
  if (json_definition_ == nullptr) {
    return;
  }
  name_ = enclosing_library_->ExtractString(json_definition_, "table", "<unknown>", "name");
  size_ = enclosing_library_->ExtractUint64(json_definition_, "table", name_, "size");

  if (!json_definition_->HasMember("members")) {
    enclosing_library_->FieldNotFound("table", name_, "members");
  } else {
    auto member_arr = (*json_definition_)["members"].GetArray();
    for (auto& member : member_arr) {
      auto table_member = std::make_unique<TableMember>(enclosing_library_, &member);
      Ordinal32 ordinal = table_member->ordinal();
      if (ordinal >= members_.size()) {
        members_.resize(ordinal + 1);
      }
      members_[ordinal] = std::move(table_member);
    }
  }
  json_definition_ = nullptr;
}

InterfaceMethod::InterfaceMethod(const Interface& interface,
                                 const rapidjson::Value* json_definition)
    : enclosing_interface_(interface),
      name_(interface.enclosing_library()->ExtractString(json_definition, "method", "<unknown>",
                                                         "name")),
      // TODO(FIDL-524): Step 4, i.e. both ord and gen are prepared by fidlc for
      // direct consumption by the bindings.
      ordinal_(interface.enclosing_library()->ExtractUint64(json_definition, "method", name_,
                                                            "ordinal")),
      old_ordinal_(interface.enclosing_library()->ExtractUint64(json_definition, "method", name_,
                                                                "generated_ordinal")),
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
  std::string fqn(enclosing_interface_.name());
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

  if (!json_definition_.HasMember("xunion_declarations")) {
    FieldNotFound("library", name_, "xunion_declarations");
  } else {
    for (auto& xuni : json_definition_["xunion_declarations"].GetArray()) {
      xunions_.emplace(std::piecewise_construct, std::forward_as_tuple(xuni["name"].GetString()),
                       std::forward_as_tuple(new Union(this, &xuni)));
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
    tmp.second->DecodeUnionTypes();
  }
  for (const auto& tmp : xunions_) {
    tmp.second->DecodeXunionTypes();
  }
  for (const auto& interface : interfaces_) {
    for (const auto& method : interface->methods()) {
      method->request();
      method->response();
    }
  }
  return !has_errors_;
}

std::unique_ptr<Type> Library::TypeFromIdentifier(bool is_nullable, std::string& identifier,
                                                  size_t inline_size) {
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
    uni->second->DecodeUnionTypes();
    return std::make_unique<UnionType>(std::ref(*uni->second), is_nullable);
  }
  auto xuni = xunions_.find(identifier);
  if (xuni != xunions_.end()) {
    // Note: XUnion and nullable XUnion are encoded in the same way
    xuni->second->DecodeXunionTypes();
    return std::make_unique<UnionType>(std::ref(*xuni->second), is_nullable);
  }
  const Interface* ifc;
  if (GetInterfaceByName(identifier, &ifc)) {
    return std::make_unique<HandleType>();
  }
  return std::make_unique<RawType>(inline_size);
}

bool Library::GetInterfaceByName(const std::string& name, const Interface** ptr) const {
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
                                                 const char* field_name, uint64_t size) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return std::make_unique<RawType>(size);
  }
  return Type::ScalarTypeFromName((*json_definition)[field_name].GetString(), size);
}

std::unique_ptr<Type> Library::ExtractType(const rapidjson::Value* json_definition,
                                           std::string_view container_type,
                                           std::string_view container_name, const char* field_name,
                                           uint64_t size) {
  if (!json_definition->HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return std::make_unique<RawType>(size);
  }
  return Type::GetType(enclosing_loader(), (*json_definition)[field_name], size);
}

void Library::FieldNotFound(std::string_view container_type, std::string_view container_name,
                            const char* field_name) {
  has_errors_ = true;
  FXL_LOG(ERROR) << "File " << name() << " field '" << field_name << "' missing for "
                 << container_type << ' ' << container_name;
}

LibraryLoader::LibraryLoader(std::vector<std::unique_ptr<std::istream>>* library_streams,
                             LibraryReadError* err) {
  AddAll(library_streams, err);
}

bool LibraryLoader::AddAll(std::vector<std::unique_ptr<std::istream>>* library_streams,
                           LibraryReadError* err) {
  bool ok = true;
  err->value = LibraryReadError::kOk;
  // Go backwards through the streams; we refuse to load the same library twice, and the last one
  // wins.
  for (auto i = library_streams->rbegin(); i != library_streams->rend(); ++i) {
    Add(&(*i), err);
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

void LibraryLoader::Add(std::unique_ptr<std::istream>* library_stream, LibraryReadError* err) {
  err->value = LibraryReadError::kOk;
  std::string ir(std::istreambuf_iterator<char>(**library_stream), {});
  if ((*library_stream)->fail()) {
    err->value = LibraryReadError ::kIoError;
    return;
  }
  Add(ir, err);
  if (err->value != LibraryReadError::kOk) {
    FXL_LOG(ERROR) << "JSON parse error: " << rapidjson::GetParseError_En(err->parse_result.Code())
                   << " at offset " << err->parse_result.Offset();
    return;
  }
}

void LibraryLoader::AddMethod(const InterfaceMethod* method) {
  // TODO(FIDL-524): At various steps of the migration, the ordinals may be
  // the same value. Avoid creating duplicate entries.
  bool ords_are_same = method->ordinal() == method->old_ordinal();
  if (ordinal_map_[method->ordinal()] == nullptr) {
    ordinal_map_[method->ordinal()] = std::make_unique<std::vector<const InterfaceMethod*>>();
    if (!ords_are_same)
      ordinal_map_[method->old_ordinal()] = std::make_unique<std::vector<const InterfaceMethod*>>();
  }
  // Ensure composed methods come after non-composed methods.  The fidl_codec
  // libraries pick the first one they find.
  if (method->is_composed()) {
    ordinal_map_[method->ordinal()]->push_back(method);
    if (!ords_are_same)
      ordinal_map_[method->old_ordinal()]->push_back(method);
  } else {
    ordinal_map_[method->ordinal()]->insert(ordinal_map_[method->ordinal()]->begin(), method);
    if (!ords_are_same)
      ordinal_map_[method->old_ordinal()]->insert(ordinal_map_[method->old_ordinal()]->begin(),
                                                  method);
  }
}

}  // namespace fidl_codec
