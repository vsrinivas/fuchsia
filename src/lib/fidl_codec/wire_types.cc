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

  void VisitXUnionType(const XUnionType* type) override {
    VisitTypeWithMembers<std::unique_ptr<UnionMember>>(
        type, "xunion", type->xunion_definition().members(),
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

  void VisitEnumType(const EnumType* type) override {
    VisitTypeWithMembers<EnumOrBitsMember>(
        type, "enum", type->enum_definition().members(), [this](const EnumOrBitsMember& member) {
          *result_ += indent_ + "  " + member.name() + " = " + member.value_str();
          return true;
        });
  }

  void VisitBitsType(const BitsType* type) override {
    VisitTypeWithMembers<EnumOrBitsMember>(
        type, "bits", type->bits_definition().members(), [this](const EnumOrBitsMember& member) {
          *result_ += indent_ + "  " + member.name() + " = " + member.value_str();
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

bool Type::ValueEquals(const uint8_t* /*bytes*/, size_t /*length*/,
                       const std::string& /*value*/) const {
  FXL_LOG(FATAL) << "Equality operator for type not implemented";
  return false;
}

bool Type::ValueHas(const uint8_t* /*bytes*/, const std::string& /*value*/) const {
  FXL_LOG(FATAL) << "ValueHas not implemented";
  return false;
}

size_t Type::InlineSize(bool unions_are_xunions) const {
  FXL_LOG(FATAL) << "Size for type not implemented";
  return 0;
}

std::unique_ptr<Value> Type::Decode(MessageDecoder* /*decoder*/, uint64_t /*offset*/) const {
  FXL_LOG(ERROR) << "Decode not implemented for field";
  return nullptr;
}

std::unique_ptr<Value> RawType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return std::make_unique<RawValue>(this, decoder->CopyAddress(offset, inline_size_));
}

void RawType::Visit(TypeVisitor* visitor) const { visitor->VisitRawType(this); }

std::unique_ptr<Value> StringType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t string_length = 0;
  decoder->GetValueAt(offset, &string_length);
  // Here, we test two conditions:
  //  - the string is a little bit too big and there is not enough data remaining.
  //  - the string is huge (typically max ulong) and wouldn't fit in the whole buffer.
  //    In that case, the first condition is not triggered because adding offset to this
  //    huge number overflows and creates a small number.
  if ((offset + string_length > decoder->num_bytes()) || (string_length > decoder->num_bytes())) {
    decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                        << ": Not enough data for string (missing "
                        << (offset + string_length - decoder->num_bytes()) << " bytes)\n";
    return std::make_unique<StringValue>(this, 0);
  }
  offset += sizeof(string_length);

  auto result = std::make_unique<StringValue>(this, string_length);

  // Don't need to check return value because the effects of returning false are
  // dealt with in DecodeNullable.
  result->DecodeNullable(decoder, offset, string_length);
  return result;
}

void StringType::Visit(TypeVisitor* visitor) const { visitor->VisitStringType(this); }

std::unique_ptr<Value> BoolType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  auto byte = decoder->GetAddress(offset, sizeof(uint8_t));
  return std::make_unique<BoolValue>(this, byte ? std::optional(*byte) : std::nullopt);
}

void BoolType::Visit(TypeVisitor* visitor) const { visitor->VisitBoolType(this); };

size_t StructType::InlineSize(bool unions_are_xunions) const {
  return nullable_ ? sizeof(uintptr_t) : struct_.Size(unions_are_xunions);
}

std::unique_ptr<Value> StructType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return struct_.DecodeStruct(decoder, this, offset, nullable_);
}

void StructType::Visit(TypeVisitor* visitor) const { visitor->VisitStructType(this); }

size_t TableType::InlineSize(bool unions_are_xunions) const { return table_.size(); }

std::unique_ptr<Value> TableType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t size = 0;
  decoder->GetValueAt(offset, &size);
  offset += sizeof(size);

  auto result = std::make_unique<TableValue>(this, table_, size);
  if (result->DecodeNullable(decoder, offset, size * 2 * sizeof(uint64_t))) {
    if (result->IsNull()) {
      decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                          << ": Invalid null value for table pointer\n";
    }
  }
  return result;
}

void TableType::Visit(TypeVisitor* visitor) const { visitor->VisitTableType(this); }

UnionType::UnionType(const Union& uni, bool nullable) : union_(uni), nullable_(nullable) {}

size_t UnionType::InlineSize(bool unions_are_xunions) const {
  if (unions_are_xunions) {
    // In v1, unions are encoded as xunion. The inline size is the size of an envelope which
    // is always 24 bytes.
    return 24;
  }
  return nullable_ ? sizeof(uintptr_t) : union_.size();
}

std::unique_ptr<Value> UnionType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return decoder->unions_are_xunions() ? union_.DecodeXUnion(decoder, this, offset, nullable_)
                                       : union_.DecodeUnion(decoder, this, offset, nullable_);
}

void UnionType::Visit(TypeVisitor* visitor) const { visitor->VisitUnionType(this); }

XUnionType::XUnionType(const XUnion& uni, bool nullable) : xunion_(uni), nullable_(nullable) {}

size_t XUnionType::InlineSize(bool unions_are_xunions) const { return xunion_.size(); }

std::unique_ptr<Value> XUnionType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return xunion_.DecodeXUnion(decoder, this, offset, nullable_);
}

void XUnionType::Visit(TypeVisitor* visitor) const { visitor->VisitXUnionType(this); }

ElementSequenceType::ElementSequenceType(std::unique_ptr<Type>&& component_type)
    : component_type_(std::move(component_type)) {
  FXL_DCHECK(component_type_.get() != nullptr);
}

void ElementSequenceType::Visit(TypeVisitor* visitor) const {
  visitor->VisitElementSequenceType(this);
}

ArrayType::ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count)
    : ElementSequenceType(std::move(component_type)), count_(count) {}

std::unique_ptr<Value> ArrayType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  auto result = std::make_unique<ArrayValue>(this);
  for (uint64_t i = 0; i < count_; ++i) {
    result->AddValue(component_type_->Decode(decoder, offset));
    offset += component_type_->InlineSize(decoder->unions_are_xunions());
  }
  return result;
}

void ArrayType::Visit(TypeVisitor* visitor) const { visitor->VisitArrayType(this); }

VectorType::VectorType(std::unique_ptr<Type>&& component_type)
    : ElementSequenceType(std::move(component_type)) {}

std::unique_ptr<Value> VectorType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t size = 0;
  decoder->GetValueAt(offset, &size);
  offset += sizeof(size);

  auto result = std::make_unique<VectorValue>(this, size);

  // Don't need to check return value because the effects of returning false are
  // dealt with in DecodeNullable.
  result->DecodeNullable(decoder, offset, size * component_type_->InlineSize(decoder->unions_are_xunions()));
  return result;
}

void VectorType::Visit(TypeVisitor* visitor) const { visitor->VisitVectorType(this); }

EnumType::EnumType(const Enum& e) : enum_(e) {}

std::unique_ptr<Value> EnumType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return std::make_unique<EnumValue>(this, decoder->CopyAddress(offset, enum_.size()), enum_);
}

void EnumType::Visit(TypeVisitor* visitor) const { visitor->VisitEnumType(this); }

BitsType::BitsType(const Bits& b) : bits_(b) {}

std::unique_ptr<Value> BitsType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return std::make_unique<BitsValue>(this, decoder->CopyAddress(offset, bits_.size()), bits_);
}

void BitsType::Visit(TypeVisitor* visitor) const { visitor->VisitBitsType(this); }

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
  return std::make_unique<HandleValue>(this, handle_info);
}

void HandleType::Visit(TypeVisitor* visitor) const { visitor->VisitHandleType(this); }

std::unique_ptr<Type> Type::ScalarTypeFromName(const std::string& type_name, size_t inline_size) {
  static std::map<std::string, std::function<std::unique_ptr<Type>()>> scalar_type_map_{
      {"bool", []() { return std::make_unique<BoolType>(); }},
      {"float32", []() { return std::make_unique<Float32Type>(); }},
      {"float64", []() { return std::make_unique<Float64Type>(); }},
      {"int8", []() { return std::make_unique<Int8Type>(); }},
      {"int16", []() { return std::make_unique<Int16Type>(); }},
      {"int32", []() { return std::make_unique<Int32Type>(); }},
      {"int64", []() { return std::make_unique<Int64Type>(); }},
      {"uint8", []() { return std::make_unique<Uint8Type>(); }},
      {"uint16", []() { return std::make_unique<Uint16Type>(); }},
      {"uint32", []() { return std::make_unique<Uint32Type>(); }},
      {"uint64", []() { return std::make_unique<Uint64Type>(); }},
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
  if (kind == "array") {
    const rapidjson::Value& element_type = type["element_type"];
    uint32_t element_count = std::strtol(type["element_count"].GetString(), nullptr, kDecimalBase);
    return std::make_unique<ArrayType>(GetType(loader, element_type, 0), element_count);
  }
  if (kind == "vector") {
    const rapidjson::Value& element_type = type["element_type"];
    return std::make_unique<VectorType>(GetType(loader, element_type, 0));
  }
  if (kind == "string") {
    return std::make_unique<StringType>();
  }
  if (kind == "handle") {
    return std::make_unique<HandleType>();
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

void Float32Type::Visit(TypeVisitor* visitor) const { visitor->VisitFloat32Type(this); }
void Float64Type::Visit(TypeVisitor* visitor) const { visitor->VisitFloat64Type(this); }
void Int8Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt8Type(this); }
void Int16Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt16Type(this); }
void Int32Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt32Type(this); }
void Int64Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt64Type(this); }
void Uint8Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint8Type(this); }
void Uint16Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint16Type(this); }
void Uint32Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint32Type(this); }
void Uint64Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint64Type(this); }

}  // namespace fidl_codec
