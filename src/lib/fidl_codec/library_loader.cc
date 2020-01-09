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

EnumOrBits::EnumOrBits(const rapidjson::Value& value) : value_(value) {}

EnumOrBits::~EnumOrBits() = default;

void EnumOrBits::DecodeTypes(bool is_scalar, const std::string& supertype_name,
                             Library* enclosing_library) {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = enclosing_library->ExtractString(value_, supertype_name, "<unknown>", "name");
  if (is_scalar) {
    type_ = enclosing_library->ExtractScalarType(value_, supertype_name, name_, "type", 0);
  } else {
    type_ = enclosing_library->ExtractType(value_, supertype_name, name_, "type", 0);
  }

  if (!value_.HasMember("members")) {
    enclosing_library->FieldNotFound(supertype_name, name_, "members");
  } else {
    if (value_.HasMember("members")) {
      for (auto& member : value_["members"].GetArray()) {
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

  size_ = type_->InlineSize(false);
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
                         const rapidjson::Value& value, bool for_xunion)
    : union_definition_(union_definition),
      reserved_(enclosing_library->ExtractBool(value, "table member", "<unknown>", "reserved")),
      name_(reserved_
                ? "<reserved>"
                : enclosing_library->ExtractString(value, "union member", "<unknown>", "name")),
      offset_(reserved_ ? 0
                        : enclosing_library->ExtractUint64(value, "union member", name_, "offset")),
      size_(reserved_ ? 0 : enclosing_library->ExtractUint64(value, "union member", name_, "size")),
      ordinal_(for_xunion
                   ? enclosing_library->ExtractUint32(value, "union member", name_, "ordinal")
                   : (value.HasMember("xunion_ordinal")
                          ? enclosing_library->ExtractUint32(value, "union member", name_,
                                                             "xunion_ordinal")
                          : 0)),
      type_(reserved_
                ? std::make_unique<RawType>(0)
                : enclosing_library->ExtractType(value, "union member", name_, "type", size_)) {}

UnionMember::~UnionMember() = default;

Union::Union(Library* enclosing_library, const rapidjson::Value& value)
    : enclosing_library_(enclosing_library), value_(value) {}

void Union::DecodeTypes(bool for_xunion) {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = enclosing_library_->ExtractString(value_, "union", "<unknown>", "name");
  alignment_ = enclosing_library_->ExtractUint64(value_, "union", name_, "alignment");
  size_ = enclosing_library_->ExtractUint64(value_, "union", name_, "size");

  if (!value_.HasMember("members")) {
    enclosing_library_->FieldNotFound("union", name_, "members");
  } else {
    auto member_arr = value_["members"].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.push_back(
          std::make_unique<UnionMember>(*this, enclosing_library_, member, for_xunion));
    }
  }
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

StructMember::StructMember(Library* enclosing_library, const rapidjson::Value& value)
    : name_(enclosing_library->ExtractString(value, "struct member", "<unknown>", "name")),
      size_(enclosing_library->ExtractUint64(value, "struct member", name_, "size")),
      type_(enclosing_library->ExtractType(value, "struct member", name_, "type", size_)) {
  if (!value.HasMember("field_shape_old")) {
    enclosing_library->FieldNotFound("struct member", name_, "field_shape_old");
  } else {
    const rapidjson::Value& v0 = value["field_shape_old"];
    v0_offset_ = enclosing_library->ExtractUint64(v0, "struct member", name_, "offset");
  }
  if (!value.HasMember("field_shape_v1")) {
    enclosing_library->FieldNotFound("struct member", name_, "field_shape_v1");
  } else {
    const rapidjson::Value& v1 = value["field_shape_v1"];
    v1_offset_ = enclosing_library->ExtractUint64(v1, "struct member", name_, "offset");
  }
}

StructMember::~StructMember() = default;

uint64_t StructMember::Offset(MessageDecoder* decoder) const {
  return decoder->unions_are_xunions() ? v1_offset_ : v0_offset_;
}

Struct::Struct(Library* enclosing_library, const rapidjson::Value& value)
    : enclosing_library_(enclosing_library), value_(value) {}

void Struct::DecodeStructTypes() {
  if (decoded_) {
    return;
  }
  DecodeTypes("struct", "size", "members", "type_shape_old", "type_shape_v1");
}

void Struct::DecodeRequestTypes() {
  if (decoded_) {
    return;
  }
  DecodeTypes("request", "maybe_request_size", "maybe_request", "maybe_request_type_shape_old",
              "maybe_request_type_shape_v1");
}

void Struct::DecodeResponseTypes() {
  if (decoded_) {
    return;
  }
  DecodeTypes("response", "maybe_response_size", "maybe_response", "maybe_response_type_shape_old",
              "maybe_response_type_shape_v1");
}

uint32_t Struct::Size(bool unions_are_xunions) const {
  return unions_are_xunions ? v1_size_ : v0_size_;
}

void Struct::DecodeTypes(std::string_view container_name, const char* size_name,
                         const char* member_name, const char* v0_name, const char* v1_name) {
  FXL_DCHECK(!decoded_);
  decoded_ = true;
  name_ = enclosing_library_->ExtractString(value_, container_name, "<unknown>", "name");

  if (!value_.HasMember(v0_name)) {
    enclosing_library_->FieldNotFound(container_name, name_, v0_name);
  } else {
    const rapidjson::Value& v0 = value_[v0_name];
    v0_size_ = enclosing_library_->ExtractUint64(v0, container_name, name_, "inline_size");
  }

  if (!value_.HasMember(v1_name)) {
    enclosing_library_->FieldNotFound(container_name, name_, v1_name);
  } else {
    const rapidjson::Value& v1 = value_[v1_name];
    v1_size_ = enclosing_library_->ExtractUint64(v1, container_name, name_, "inline_size");
  }

  if (!value_.HasMember(member_name)) {
    enclosing_library_->FieldNotFound(container_name, name_, member_name);
  } else {
    auto member_arr = value_[member_name].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.push_back(std::make_unique<StructMember>(enclosing_library_, member));
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

TableMember::TableMember(Library* enclosing_library, const rapidjson::Value& value)
    : reserved_(enclosing_library->ExtractBool(value, "table member", "<unknown>", "reserved")),
      name_(reserved_
                ? "<reserved>"
                : enclosing_library->ExtractString(value, "table member", "<unknown>", "name")),
      ordinal_(enclosing_library->ExtractUint32(value, "table member", name_, "ordinal")),
      size_(reserved_ ? 0 : enclosing_library->ExtractUint64(value, "table member", name_, "size")),
      type_(reserved_
                ? std::make_unique<RawType>(0)
                : enclosing_library->ExtractType(value, "table member", name_, "type", size_)) {}

TableMember::~TableMember() = default;

Table::Table(Library* enclosing_library, const rapidjson::Value& value)
    : enclosing_library_(enclosing_library), value_(value) {}

Table::~Table() = default;

void Table::DecodeTypes() {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = enclosing_library_->ExtractString(value_, "table", "<unknown>", "name");
  size_ = enclosing_library_->ExtractUint64(value_, "table", name_, "size");

  if (!value_.HasMember("members")) {
    enclosing_library_->FieldNotFound("table", name_, "members");
  } else {
    auto member_arr = value_["members"].GetArray();
    for (auto& member : member_arr) {
      auto table_member = std::make_unique<TableMember>(enclosing_library_, member);
      Ordinal32 ordinal = table_member->ordinal();
      if (ordinal >= members_.size()) {
        members_.resize(ordinal + 1);
      }
      members_[ordinal] = std::move(table_member);
    }
  }
}

InterfaceMethod::InterfaceMethod(const Interface& interface, const rapidjson::Value& value)
    : enclosing_interface_(interface),
      name_(interface.enclosing_library()->ExtractString(value, "method", "<unknown>", "name")),
      // TODO(FIDL-524): Step 4, i.e. both ord and gen are prepared by fidlc for
      // direct consumption by the bindings.
      ordinal_(interface.enclosing_library()->ExtractUint64(value, "method", name_, "ordinal")),
      old_ordinal_(interface.enclosing_library()->ExtractUint64(value, "method", name_,
                                                                "generated_ordinal")),
      is_composed_(
          interface.enclosing_library()->ExtractBool(value, "method", name_, "is_composed")) {
  if (interface.enclosing_library()->ExtractBool(value, "method", name_, "has_request")) {
    request_ = std::unique_ptr<Struct>(new Struct(interface.enclosing_library(), value));
  }

  if (interface.enclosing_library()->ExtractBool(value, "method", name_, "has_response")) {
    response_ = std::unique_ptr<Struct>(new Struct(interface.enclosing_library(), value));
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
                 std::map<Ordinal64, std::unique_ptr<std::vector<const InterfaceMethod*>>>& index)
    : enclosing_loader_(enclosing_loader), backing_document_(std::move(document)) {
  auto interfaces_array = backing_document_["interface_declarations"].GetArray();
  interfaces_.reserve(interfaces_array.Size());

  for (auto& decl : interfaces_array) {
    interfaces_.emplace_back(new Interface(this, decl));
    interfaces_.back()->AddMethodsToIndex(index);
  }
}

Library::~Library() { enclosing_loader()->Delete(this); }

void Library::DecodeTypes() {
  if (decoded_) {
    return;
  }
  decoded_ = true;
  name_ = ExtractString(backing_document_, "library", "<unknown>", "name");

  if (!backing_document_.HasMember("enum_declarations")) {
    FieldNotFound("library", name_, "enum_declarations");
  } else {
    for (auto& enu : backing_document_["enum_declarations"].GetArray()) {
      enums_.emplace(std::piecewise_construct, std::forward_as_tuple(enu["name"].GetString()),
                     std::forward_as_tuple(new Enum(enu)));
    }
  }

  if (!backing_document_.HasMember("bits_declarations")) {
    FieldNotFound("library", name_, "bits_declarations");
  } else {
    for (auto& bits : backing_document_["bits_declarations"].GetArray()) {
      bits_.emplace(std::piecewise_construct, std::forward_as_tuple(bits["name"].GetString()),
                    std::forward_as_tuple(new Bits(bits)));
    }
  }

  if (!backing_document_.HasMember("struct_declarations")) {
    FieldNotFound("library", name_, "struct_declarations");
  } else {
    for (auto& str : backing_document_["struct_declarations"].GetArray()) {
      structs_.emplace(std::piecewise_construct, std::forward_as_tuple(str["name"].GetString()),
                       std::forward_as_tuple(new Struct(this, str)));
    }
  }

  if (!backing_document_.HasMember("table_declarations")) {
    FieldNotFound("library", name_, "table_declarations");
  } else {
    for (auto& tab : backing_document_["table_declarations"].GetArray()) {
      tables_.emplace(std::piecewise_construct, std::forward_as_tuple(tab["name"].GetString()),
                      std::forward_as_tuple(new Table(this, tab)));
    }
  }

  if (!backing_document_.HasMember("union_declarations")) {
    FieldNotFound("library", name_, "union_declarations");
  } else {
    for (auto& uni : backing_document_["union_declarations"].GetArray()) {
      unions_.emplace(std::piecewise_construct, std::forward_as_tuple(uni["name"].GetString()),
                      std::forward_as_tuple(new Union(this, uni)));
    }
  }

  if (!backing_document_.HasMember("xunion_declarations")) {
    FieldNotFound("library", name_, "xunion_declarations");
  } else {
    for (auto& xuni : backing_document_["xunion_declarations"].GetArray()) {
      xunions_.emplace(std::piecewise_construct, std::forward_as_tuple(xuni["name"].GetString()),
                       std::forward_as_tuple(new XUnion(this, xuni)));
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
    return std::make_unique<XUnionType>(std::ref(*xuni->second), is_nullable);
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

bool Library::ExtractBool(const rapidjson::Value& value, std::string_view container_type,
                          std::string_view container_name, const char* field_name) {
  if (!value.HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return false;
  }
  return value[field_name].GetBool();
}

std::string Library::ExtractString(const rapidjson::Value& value, std::string_view container_type,
                                   std::string_view container_name, const char* field_name) {
  if (!value.HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return "<unknown>";
  }
  return value["name"].GetString();
}

uint64_t Library::ExtractUint64(const rapidjson::Value& value, std::string_view container_type,
                                std::string_view container_name, const char* field_name) {
  if (!value.HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return 0;
  }
  return std::strtoll(value[field_name].GetString(), nullptr, kDecimalBase);
}

uint32_t Library::ExtractUint32(const rapidjson::Value& value, std::string_view container_type,
                                std::string_view container_name, const char* field_name) {
  if (!value.HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return 0;
  }
  return std::strtoll(value[field_name].GetString(), nullptr, kDecimalBase);
}

std::unique_ptr<Type> Library::ExtractScalarType(const rapidjson::Value& value,
                                                 std::string_view container_type,
                                                 std::string_view container_name,
                                                 const char* field_name, uint64_t size) {
  if (!value.HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return std::make_unique<RawType>(size);
  }
  return Type::ScalarTypeFromName(value[field_name].GetString(), size);
}

std::unique_ptr<Type> Library::ExtractType(const rapidjson::Value& value,
                                           std::string_view container_type,
                                           std::string_view container_name, const char* field_name,
                                           uint64_t size) {
  if (!value.HasMember(field_name)) {
    FieldNotFound(container_type, container_name, field_name);
    return std::make_unique<RawType>(size);
  }
  return Type::GetType(enclosing_loader(), value[field_name], size);
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

}  // namespace fidl_codec
