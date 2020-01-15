// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_types.h"

#include <zircon/fidl.h>

#include "rapidjson/error/en.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/type_visitor.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fxl/logging.h"

// See wire_types.h for details.

namespace fidl_codec {
namespace {

class ToStringVisitor : public TypeVisitor {
 public:
  enum ExpandLevels {
    kNone,
    kOne,
    kAll,
  };

  explicit ToStringVisitor(const std::string& indent, ExpandLevels levels, std::string* result)
      : indent_(indent), levels_(levels), result_(result) {}
  ~ToStringVisitor() = default;

 private:
  ExpandLevels NextExpandLevels() {
    if (levels_ == ExpandLevels::kAll) {
      return ExpandLevels::kAll;
    }

    return ExpandLevels::kNone;
  }

  template <typename T>
  void VisitTypeWithMembers(const Type* type, const std::string& name,
                            const std::vector<T>& members, fit::function<bool(const T&)> body) {
    *result_ += name + " ";
    VisitType(type);

    if (levels_ == ExpandLevels::kNone) {
      return;
    }

    *result_ += " {";

    if (members.empty()) {
      *result_ += "}";
      return;
    }

    *result_ += "\n";

    for (const auto& member : members) {
      if (body(member)) {
        *result_ += ";\n";
      }
    }

    *result_ += indent_ + "}";
  }

  void VisitType(const Type* type) override { *result_ += type->Name(); }

  void VisitEnumType(const EnumType* type) override {
    VisitTypeWithMembers<EnumOrBitsMember>(type, "enum", type->enum_definition().members(),
                                           [this](const EnumOrBitsMember& member) {
                                             *result_ += indent_ + "  " + member.name() + " = ";
                                             if (member.negative()) {
                                               *result_ += "-";
                                             }
                                             *result_ += std::to_string(member.absolute_value());
                                             return true;
                                           });
  }

  void VisitBitsType(const BitsType* type) override {
    VisitTypeWithMembers<EnumOrBitsMember>(
        type, "bits", type->bits_definition().members(), [this](const EnumOrBitsMember& member) {
          *result_ +=
              indent_ + "  " + member.name() + " = " + std::to_string(member.absolute_value());
          return true;
        });
  }

  void VisitUnionType(const UnionType* type) override {
    VisitTypeWithMembers<std::unique_ptr<UnionMember>>(
        type, "union", type->union_definition().members(),
        [this](const std::unique_ptr<UnionMember>& member) {
          *result_ += indent_ + "  " + std::to_string(member->ordinal()) + ": ";
          if (member->reserved()) {
            *result_ += "reserved";
            return true;
          }

          ToStringVisitor visitor(indent_ + "  ", NextExpandLevels(), result_);
          member->type()->Visit(&visitor);

          *result_ += " " + std::string(member->name());
          return true;
        });
  }

  void VisitStructType(const StructType* type) override {
    VisitTypeWithMembers<std::unique_ptr<StructMember>>(
        type, "struct", type->struct_definition().members(),
        [this](const std::unique_ptr<StructMember>& member) {
          *result_ += indent_ + "  ";
          ToStringVisitor visitor(indent_ + "  ", NextExpandLevels(), result_);
          member->type()->Visit(&visitor);
          *result_ += " " + std::string(member->name());
          return true;
        });
  }

  void VisitArrayType(const ArrayType* type) override {
    *result_ += "array<";
    type->component_type()->Visit(this);
    *result_ += ">";
  }

  void VisitVectorType(const VectorType* type) override {
    *result_ += "vector<";
    type->component_type()->Visit(this);
    *result_ += ">";
  }

  void VisitTableType(const TableType* type) override {
    VisitTypeWithMembers<std::unique_ptr<TableMember>>(
        type, "table", type->table_definition().members(),
        [this](const std::unique_ptr<TableMember>& member) {
          if (!member) {
            return false;
          }
          *result_ += indent_ + "  ";
          *result_ += std::to_string(member->ordinal()) + ": ";
          if (member->reserved()) {
            *result_ += "reserved";
            return true;
          }
          ToStringVisitor visitor(indent_ + "  ", NextExpandLevels(), result_);
          member->type()->Visit(&visitor);
          *result_ += " " + std::string(member->name());
          return true;
        });
  }

  std::string indent_;
  ExpandLevels levels_;
  std::string* result_;
};

}  // namespace

std::string Type::ToString(bool expand) const {
  std::string ret;
  ToStringVisitor visitor(
      "", expand ? ToStringVisitor::ExpandLevels::kAll : ToStringVisitor::ExpandLevels::kOne, &ret);
  Visit(&visitor);
  return ret;
}

void Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  printer << Red << "invalid" << ResetColor;
}

std::string RawType::Name() const { return "unknown"; }

size_t RawType::InlineSize() const { return inline_size_; }

bool RawType::Nullable() const { return true; }

std::unique_ptr<Value> RawType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  const uint8_t* data = decoder->GetAddress(offset, inline_size_);
  if (data == nullptr) {
    return std::make_unique<InvalidValue>();
  }
  return std::make_unique<RawValue>(data, inline_size_);
}

void RawType::Visit(TypeVisitor* visitor) const { visitor->VisitRawType(this); }

size_t BoolType::InlineSize() const { return sizeof(uint8_t); }

std::unique_ptr<Value> BoolType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  auto byte = decoder->GetAddress(offset, sizeof(uint8_t));
  if (byte == nullptr) {
    return std::make_unique<InvalidValue>();
  }
  return std::make_unique<BoolValue>(*byte);
}

void BoolType::Visit(TypeVisitor* visitor) const { visitor->VisitBoolType(this); };

std::string Int8Type::Name() const { return "int8"; }

void Int8Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt8Type(this); }

std::string Int16Type::Name() const { return "int16"; }

void Int16Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt16Type(this); }

std::string Int32Type::Name() const { return "int32"; }

void Int32Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt32Type(this); }

std::string Int64Type::Name() const { return "int64"; }

void Int64Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt64Type(this); }

std::string Uint8Type::Name() const { return "uint8"; }

void Uint8Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint8Type(this); }

std::string Uint16Type::Name() const { return "uint16"; }

void Uint16Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint16Type(this); }

std::string Uint32Type::Name() const { return "uint32"; }

void Uint32Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint32Type(this); }

std::string Uint64Type::Name() const { return "uint64"; }

void Uint64Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint64Type(this); }

std::string Float32Type::Name() const { return "float32"; }

void Float32Type::Visit(TypeVisitor* visitor) const { visitor->VisitFloat32Type(this); }

std::string Float64Type::Name() const { return "float64"; }

void Float64Type::Visit(TypeVisitor* visitor) const { visitor->VisitFloat64Type(this); }

std::string StringType::Name() const { return "string"; }

size_t StringType::InlineSize() const { return sizeof(uint64_t) + sizeof(uint64_t); }

bool StringType::Nullable() const { return true; }

std::unique_ptr<Value> StringType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t string_length = 0;
  if (!decoder->GetValueAt(offset, &string_length)) {
    return std::make_unique<InvalidValue>();
  }
  offset += sizeof(string_length);

  bool is_null;
  uint64_t nullable_offset;
  if (!decoder->DecodeNullableHeader(offset, string_length, &is_null, &nullable_offset)) {
    return std::make_unique<InvalidValue>();
  }
  if (is_null) {
    return std::make_unique<NullValue>();
  }
  auto data = reinterpret_cast<const char*>(decoder->GetAddress(nullable_offset, string_length));
  if (data == nullptr) {
    return std::make_unique<InvalidValue>();
  }
  return std::make_unique<StringValue>(std::string_view(data, string_length));
}

void StringType::Visit(TypeVisitor* visitor) const { visitor->VisitStringType(this); }

std::string HandleType::Name() const { return "handle"; }

size_t HandleType::InlineSize() const { return sizeof(zx_handle_t); }

std::unique_ptr<Value> HandleType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  zx_handle_t handle = FIDL_HANDLE_ABSENT;
  decoder->GetValueAt(offset, &handle);
  if ((handle != FIDL_HANDLE_ABSENT) && (handle != FIDL_HANDLE_PRESENT)) {
    decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                        << ": Invalid value <" << std::hex << handle << std::dec
                        << "> for handle\n";
    handle = FIDL_HANDLE_ABSENT;
  }
  zx_handle_info_t handle_info;
  if (handle == FIDL_HANDLE_ABSENT) {
    handle_info.handle = FIDL_HANDLE_ABSENT;
    handle_info.type = ZX_OBJ_TYPE_NONE;
    handle_info.rights = 0;
  } else {
    handle_info = decoder->GetNextHandle();
  }
  return std::make_unique<HandleValue>(handle_info);
}

void HandleType::Visit(TypeVisitor* visitor) const { visitor->VisitHandleType(this); }

std::string EnumType::Name() const { return enum_definition_.name(); }

size_t EnumType::InlineSize() const { return enum_definition_.size(); }

std::unique_ptr<Value> EnumType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return enum_definition_.type()->Decode(decoder, offset);
}

void EnumType::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    printer << Blue << enum_definition_.GetName(absolute, negative) << ResetColor;
  }
}

void EnumType::Visit(TypeVisitor* visitor) const { visitor->VisitEnumType(this); }

std::string BitsType::Name() const { return bits_definition_.name(); }

size_t BitsType::InlineSize() const { return bits_definition_.size(); }

std::unique_ptr<Value> BitsType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return bits_definition_.type()->Decode(decoder, offset);
}

void BitsType::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    printer << Blue << bits_definition_.GetName(absolute, negative) << ResetColor;
  }
}

void BitsType::Visit(TypeVisitor* visitor) const { visitor->VisitBitsType(this); }

std::string UnionType::Name() const { return union_definition_.name(); }

size_t UnionType::InlineSize() const {
  // In v1, unions are encoded as xunion. The inline size is the size of an envelope which
  // is always 24 bytes.
  return 24;
}

bool UnionType::Nullable() const { return nullable_; }

std::unique_ptr<Value> UnionType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  Ordinal32 ordinal = 0;
  if (decoder->GetValueAt(offset, &ordinal)) {
    if ((ordinal == 0) && !nullable_) {
      decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                          << ": Null envelope for a non nullable extensible union\n";
      return std::make_unique<InvalidValue>();
    }
  }

  offset += sizeof(uint64_t);  // Skips ordinal + padding.

  if (ordinal == 0) {
    if (!decoder->CheckNullEnvelope(offset)) {
      return std::make_unique<InvalidValue>();
    }
    return std::make_unique<NullValue>();
  }

  const UnionMember* member = union_definition_.MemberWithOrdinal(ordinal);
  if (member == nullptr) {
    return std::make_unique<InvalidValue>();
  }
  return std::make_unique<UnionValue>(*member, decoder->DecodeEnvelope(offset, member->type()));
}

void UnionType::Visit(TypeVisitor* visitor) const { visitor->VisitUnionType(this); }

std::string StructType::Name() const { return struct_definition_.name(); }

size_t StructType::InlineSize() const {
  return nullable_ ? sizeof(uintptr_t) : struct_definition_.size();
}

bool StructType::Nullable() const { return nullable_; }

std::unique_ptr<Value> StructType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  if (nullable_) {
    bool is_null;
    uint64_t nullable_offset;
    if (!decoder->DecodeNullableHeader(offset, struct_definition_.size(), &is_null,
                                       &nullable_offset)) {
      return std::make_unique<InvalidValue>();
    }
    if (is_null) {
      return std::make_unique<NullValue>();
    }
    offset = nullable_offset;
  }
  return decoder->DecodeStruct(struct_definition_, offset);
}

void StructType::Visit(TypeVisitor* visitor) const { visitor->VisitStructType(this); }

const Type* ElementSequenceType::GetComponentType() const { return component_type_.get(); }

void ElementSequenceType::Visit(TypeVisitor* visitor) const {
  visitor->VisitElementSequenceType(this);
}

bool ArrayType::IsArray() const { return true; }

std::string ArrayType::Name() const {
  return std::string("array<") + component_type_->Name() + ">";
}

size_t ArrayType::InlineSize() const { return component_type_->InlineSize() * count_; }

std::unique_ptr<Value> ArrayType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  auto result = std::make_unique<VectorValue>();
  for (uint64_t i = 0; i < count_; ++i) {
    result->AddValue(component_type_->Decode(decoder, offset));
    offset += component_type_->InlineSize();
  }
  return result;
}

void ArrayType::Visit(TypeVisitor* visitor) const { visitor->VisitArrayType(this); }

std::string VectorType::Name() const {
  return std::string("vector<") + component_type_->Name() + ">";
}

size_t VectorType::InlineSize() const { return sizeof(uint64_t) + sizeof(uint64_t); }

bool VectorType::Nullable() const { return true; }

std::unique_ptr<Value> VectorType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t element_count = 0;
  decoder->GetValueAt(offset, &element_count);
  offset += sizeof(element_count);
  bool is_null;
  uint64_t nullable_offset;
  if (!decoder->DecodeNullableHeader(offset, element_count * component_type_->InlineSize(),
                                     &is_null, &nullable_offset)) {
    return std::make_unique<InvalidValue>();
  }
  if (is_null) {
    return std::make_unique<NullValue>();
  }

  size_t component_size = component_type_->InlineSize();
  auto result = std::make_unique<VectorValue>();
  for (uint64_t i = 0;
       (i < element_count) && (nullable_offset + component_size <= decoder->num_bytes()); ++i) {
    result->AddValue(component_type_->Decode(decoder, nullable_offset));
    nullable_offset += component_size;
  }
  return result;
}

void VectorType::Visit(TypeVisitor* visitor) const { visitor->VisitVectorType(this); }

std::string TableType::Name() const { return table_definition_.name(); }

size_t TableType::InlineSize() const { return table_definition_.size(); }

std::unique_ptr<Value> TableType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t member_count = 0;
  decoder->GetValueAt(offset, &member_count);
  offset += sizeof(member_count);

  bool is_null;
  uint64_t nullable_offset;
  constexpr size_t kEnvelopeSize = 2 * sizeof(uint32_t) + sizeof(uint64_t);
  if (!decoder->DecodeNullableHeader(offset, member_count * kEnvelopeSize, &is_null,
                                     &nullable_offset)) {
    return std::make_unique<InvalidValue>();
  }
  if (is_null) {
    decoder->AddError() << "Tables are not nullable.";
    return std::make_unique<InvalidValue>();
  }
  auto result = std::make_unique<TableValue>(table_definition_);
  for (uint64_t i = 1; i <= member_count; ++i) {
    const TableMember* member = table_definition_.GetMember(i);
    if ((member == nullptr) || member->reserved()) {
      decoder->SkipEnvelope(nullable_offset);
    } else {
      std::unique_ptr<Value> value = decoder->DecodeEnvelope(nullable_offset, member->type());
      if (!value->IsNull()) {
        result->AddMember(member, std::move(value));
      }
    }
    nullable_offset += kEnvelopeSize;
  }
  return result;
}

void TableType::Visit(TypeVisitor* visitor) const { visitor->VisitTableType(this); }

std::unique_ptr<Type> Type::ScalarTypeFromName(const std::string& type_name, size_t inline_size) {
  static std::map<std::string, std::function<std::unique_ptr<Type>()>> scalar_type_map_{
      {"bool", []() { return std::make_unique<BoolType>(); }},
      {"int8", []() { return std::make_unique<Int8Type>(); }},
      {"int16", []() { return std::make_unique<Int16Type>(); }},
      {"int32", []() { return std::make_unique<Int32Type>(); }},
      {"int64", []() { return std::make_unique<Int64Type>(); }},
      {"uint8", []() { return std::make_unique<Uint8Type>(); }},
      {"uint16", []() { return std::make_unique<Uint16Type>(); }},
      {"uint32", []() { return std::make_unique<Uint32Type>(); }},
      {"uint64", []() { return std::make_unique<Uint64Type>(); }},
      {"float32", []() { return std::make_unique<Float32Type>(); }},
      {"float64", []() { return std::make_unique<Float64Type>(); }},
  };
  auto it = scalar_type_map_.find(type_name);
  if (it != scalar_type_map_.end()) {
    return it->second();
  }
  return std::make_unique<RawType>(inline_size);
}

std::unique_ptr<Type> Type::TypeFromPrimitive(const rapidjson::Value& type, size_t inline_size) {
  if (!type.HasMember("subtype")) {
    FXL_LOG(ERROR) << "Invalid type";
    return std::make_unique<RawType>(inline_size);
  }

  std::string subtype = type["subtype"].GetString();
  return ScalarTypeFromName(subtype, inline_size);
}

std::unique_ptr<Type> Type::TypeFromIdentifier(LibraryLoader* loader, const rapidjson::Value& type,
                                               size_t inline_size) {
  if (!type.HasMember("identifier")) {
    FXL_LOG(ERROR) << "Invalid type";
    return std::make_unique<RawType>(inline_size);
  }
  std::string id = type["identifier"].GetString();
  size_t split_index = id.find('/');
  std::string library_name = id.substr(0, split_index);
  Library* library = loader->GetLibraryFromName(library_name);
  if (library == nullptr) {
    FXL_LOG(ERROR) << "Unknown type for identifier: " << library_name;
    return std::make_unique<RawType>(inline_size);
  }

  bool is_nullable = false;
  if (type.HasMember("nullable")) {
    is_nullable = type["nullable"].GetBool();
  }
  return library->TypeFromIdentifier(is_nullable, id, inline_size);
}

std::unique_ptr<Type> Type::GetType(LibraryLoader* loader, const rapidjson::Value& type,
                                    size_t inline_size) {
  if (!type.HasMember("kind")) {
    FXL_LOG(ERROR) << "Invalid type";
    return std::make_unique<RawType>(inline_size);
  }
  std::string kind = type["kind"].GetString();
  if (kind == "string") {
    return std::make_unique<StringType>();
  }
  if (kind == "handle") {
    return std::make_unique<HandleType>();
  }
  if (kind == "array") {
    const rapidjson::Value& element_type = type["element_type"];
    uint32_t element_count = std::strtol(type["element_count"].GetString(), nullptr, kDecimalBase);
    return std::make_unique<ArrayType>(GetType(loader, element_type, 0), element_count);
  }
  if (kind == "vector") {
    const rapidjson::Value& element_type = type["element_type"];
    return std::make_unique<VectorType>(GetType(loader, element_type, 0));
  }
  if (kind == "request") {
    return std::make_unique<HandleType>();
  }
  if (kind == "primitive") {
    return Type::TypeFromPrimitive(type, inline_size);
  }
  if (kind == "identifier") {
    return Type::TypeFromIdentifier(loader, type, inline_size);
  }
  FXL_LOG(ERROR) << "Invalid type " << kind;
  return std::make_unique<RawType>(inline_size);
}

}  // namespace fidl_codec
