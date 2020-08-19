// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/wire_types.h"

#include <zircon/fidl.h>

#include <rapidjson/error/en.h>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/logger.h"
#include "src/lib/fidl_codec/type_visitor.h"
#include "src/lib/fidl_codec/wire_object.h"

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

std::string FidlMethodNameToCpp(std::string_view identifier) {
  std::string result(identifier);
  size_t start = 0;
  while ((start = result.find_first_of("./", start)) != std::string::npos) {
    result.replace(start, 1, "::");
    start += 2;
  }
  return result;
}

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

std::string InvalidType::Name() const { return "unknown"; }

size_t InvalidType::InlineSize() const { return 0; }

std::unique_ptr<Value> InvalidType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return std::make_unique<InvalidValue>();
}

void InvalidType::Visit(TypeVisitor* visitor) const { visitor->VisitInvalidType(this); }

size_t BoolType::InlineSize() const { return sizeof(uint8_t); }

std::unique_ptr<Value> BoolType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  auto byte = decoder->GetAddress(offset, sizeof(uint8_t));
  if (byte == nullptr) {
    return std::make_unique<InvalidValue>();
  }
  return std::make_unique<BoolValue>(*byte);
}

void BoolType::Visit(TypeVisitor* visitor) const { visitor->VisitBoolType(this); };

std::string Int8Type::Name() const {
  switch (kind_) {
    case Kind::kDecimal:
      return "int8";
    case Kind::kChar:
      return "char";
  }
}

void Int8Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    switch (kind_) {
      case Kind::kChar:
      case Kind::kDecimal:
        printer << Blue;
        if (negative) {
          printer << '-';
        }
        printer << absolute << ResetColor;
        break;
    }
  }
}

void Int8Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt8Type(this); }

std::string Int16Type::Name() const { return "int16"; }

void Int16Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    switch (kind_) {
      case Kind::kDecimal:
        printer << Blue;
        if (negative) {
          printer << '-';
        }
        printer << absolute << ResetColor;
        break;
    }
  }
}

void Int16Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt16Type(this); }

std::string Int32Type::Name() const { return "int32"; }

void Int32Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    switch (kind_) {
      case Kind::kDecimal:
        printer << Blue;
        if (negative) {
          printer << '-';
        }
        printer << absolute << ResetColor;
        break;
    }
  }
}

void Int32Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt32Type(this); }

std::string Int64Type::Name() const {
  switch (kind_) {
    case Kind::kDecimal:
      return "int64";
    case Kind::kDuration:
      return "zx.duration";
    case Kind::kTime:
      return "zx.time";
  }
}

void Int64Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    switch (kind_) {
      case Kind::kDecimal:
        printer << Blue;
        if (negative) {
          printer << '-';
        }
        printer << absolute << ResetColor;
        break;
      case Kind::kDuration:
        if (negative) {
          absolute = -absolute;
        }
        printer.DisplayDuration(static_cast<zx_duration_t>(absolute));
        break;
      case Kind::kTime:
        if (negative) {
          absolute = -absolute;
        }
        printer.DisplayTime(static_cast<zx_time_t>(absolute));
        break;
    }
  }
}

void Int64Type::Visit(TypeVisitor* visitor) const { visitor->VisitInt64Type(this); }

std::string Uint8Type::Name() const {
  switch (kind_) {
    case Kind::kDecimal:
    case Kind::kHexaDecimal:
      return "uint8";
    case Kind::kPacketGuestVcpuType:
      return "zx.packet_guest_vcpu::type";
  }
}

void Uint8Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    FX_DCHECK(!negative);
    switch (kind_) {
      case Kind::kDecimal:
        printer << Blue << absolute << ResetColor;
        break;
      case Kind::kHexaDecimal:
        printer.DisplayHexa8(absolute);
        break;
      case Kind::kPacketGuestVcpuType:
        printer.DisplayPacketGuestVcpuType(absolute);
    }
  }
}

void Uint8Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint8Type(this); }

std::string Uint16Type::Name() const {
  switch (kind_) {
    case Kind::kDecimal:
    case Kind::kHexaDecimal:
      return "uint16";
    case Kind::kPacketPageRequestCommand:
      return "zx.packet_page_request::command";
  }
}

void Uint16Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    FX_DCHECK(!negative);
    switch (kind_) {
      case Kind::kDecimal:
        printer << Blue << absolute << ResetColor;
        break;
      case Kind::kHexaDecimal:
        printer.DisplayHexa16(absolute);
        break;
      case Kind::kPacketPageRequestCommand:
        printer.DisplayPacketPageRequestCommand(absolute);
        break;
    }
  }
}

void Uint16Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint16Type(this); }

std::string Uint32Type::Name() const {
  switch (kind_) {
    case Kind::kBtiPerm:
      return "zx.bti_perm";
    case Kind::kCachePolicy:
      return "zx.cache_policy";
    case Kind::kClock:
      return "zx.clock";
    case Kind::kDecimal:
    case Kind::kHexaDecimal:
      return "uint32";
    case Kind::kExceptionState:
      return "zx.exception_state";
    case Kind::kObjectInfoTopic:
      return "zx.object_info_topic";
    case Kind::kPciBarType:
      return "zx.pci_bar_type";
    case Kind::kPortPacketType:
      return "zx.port_packet::type";
    case Kind::kProfileInfoFlags:
      return "zx.profile_info_flags";
    case Kind::kPropType:
      return "zx.prop_type";
    case Kind::kRights:
      return "zx.rights";
    case Kind::kSignals:
      return "signals";
    case Kind::kStatus:
      return "zx.status";
  }
}

void Uint32Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    FX_DCHECK(!negative);
    switch (kind_) {
      case Kind::kBtiPerm:
        printer.DisplayBtiPerm(absolute);
        break;
      case Kind::kCachePolicy:
        printer.DisplayCachePolicy(absolute);
        break;
      case Kind::kClock:
        printer.DisplayClock(absolute);
        break;
      case Kind::kDecimal:
        printer << Blue << absolute << ResetColor;
        break;
      case Kind::kExceptionState:
        printer.DisplayExceptionState(absolute);
        break;
      case Kind::kHexaDecimal:
        printer.DisplayHexa32(absolute);
        break;
      case Kind::kObjectInfoTopic:
        printer.DisplayObjectInfoTopic(absolute);
        break;
      case Kind::kPciBarType:
        printer.DisplayPciBarType(absolute);
        break;
      case Kind::kProfileInfoFlags:
        printer.DisplayProfileInfoFlags(absolute);
        break;
      case Kind::kPropType:
        printer.DisplayPropType(absolute);
        break;
      case Kind::kPortPacketType:
        printer.DisplayPortPacketType(absolute);
        break;
      case Kind::kRights:
        printer.DisplayRights(absolute);
        break;
      case Kind::kSignals:
        printer.DisplaySignals(absolute);
        break;
      case Kind::kStatus:
        printer.DisplayStatus(absolute);
        break;
    }
  }
}

void Uint32Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint32Type(this); }

std::string Uint64Type::Name() const {
  switch (kind_) {
    case Kind::kDecimal:
    case Kind::kHexaDecimal:
      return "uint64";
    case Kind::kGpAddr:
      return "zx.gpaddr";
    case Kind::kPaddr:
      return "zx.paddr";
    case Kind::kSize:
      return "size";
    case Kind::kUintptr:
      return "uintptr";
    case Kind::kVaddr:
      return "zx.vaddr";
  }
}

void Uint64Type::PrettyPrint(const Value* value, PrettyPrinter& printer) const {
  uint64_t absolute;
  bool negative;
  if (!value->GetIntegerValue(&absolute, &negative)) {
    printer << Red << "invalid" << ResetColor;
  } else {
    FX_DCHECK(!negative);
    switch (kind_) {
      case Kind::kDecimal:
        printer << Blue << absolute << ResetColor;
        break;
      case Kind::kGpAddr:
        printer.DisplayGpAddr(absolute);
        break;
      case Kind::kHexaDecimal:
        printer.DisplayHexa64(absolute);
        break;
      case Kind::kPaddr:
        printer.DisplayPaddr(absolute);
        break;
      case Kind::kSize:
        printer << Blue << absolute << ResetColor;
        break;
      case Kind::kUintptr:
        printer.DisplayUintptr(absolute);
        break;
      case Kind::kVaddr:
        printer.DisplayVaddr(absolute);
        break;
    }
  }
}

void Uint64Type::Visit(TypeVisitor* visitor) const { visitor->VisitUint64Type(this); }

std::string Float32Type::Name() const { return "float32"; }

std::string Float32Type::CppName() const { return "float"; }

void Float32Type::Visit(TypeVisitor* visitor) const { visitor->VisitFloat32Type(this); }

std::string Float64Type::Name() const { return "float64"; }

std::string Float64Type::CppName() const { return "double"; }

void Float64Type::Visit(TypeVisitor* visitor) const { visitor->VisitFloat64Type(this); }

std::string StringType::Name() const { return "string"; }

std::string StringType::CppName() const { return "std::string"; }

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

std::string HandleType::CppName() const { return "zx::handle"; }

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

std::string EnumType::CppName() const { return FidlMethodNameToCpp(enum_definition_.name()); }

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

std::string BitsType::CppName() const { return FidlMethodNameToCpp(bits_definition_.name()); }

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

std::string UnionType::CppName() const { return FidlMethodNameToCpp(union_definition_.name()); }

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

std::string StructType::CppName() const { return FidlMethodNameToCpp(struct_definition_.name()); }

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

std::string ArrayType::CppName() const {
  return std::string("std::array<") + component_type_->CppName() + ", " + std::to_string(count()) +
         ">";
}

void ArrayType::PrettyPrint(PrettyPrinter& printer) const {
  printer << "array<" << Green << component_type_->Name() << ResetColor << ">";
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

std::string VectorType::CppName() const {
  return std::string("std::vector<") + component_type_->CppName() + ">";
}

void VectorType::PrettyPrint(PrettyPrinter& printer) const {
  printer << "vector<" << Green << component_type_->Name() << ResetColor << ">";
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

std::string TableType::CppName() const { return FidlMethodNameToCpp(table_definition_.name()); }

size_t TableType::InlineSize() const {
  // A table is always implemented as a size + a pointer.
  return 2 * sizeof(uint64_t);
}

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

std::string FidlMessageType::Name() const { return "fidl-message"; }

size_t FidlMessageType::InlineSize() const { return 0; }

std::unique_ptr<Value> FidlMessageType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return nullptr;
}

void FidlMessageType::Visit(TypeVisitor* visitor) const { visitor->VisitFidlMessageType(this); }

std::unique_ptr<Type> Type::ScalarTypeFromName(const std::string& type_name) {
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
  return std::make_unique<InvalidType>();
}

std::unique_ptr<Type> Type::TypeFromPrimitive(const rapidjson::Value& type) {
  if (!type.HasMember("subtype")) {
    FX_LOGS_OR_CAPTURE(ERROR) << "Invalid type";
    return std::make_unique<InvalidType>();
  }

  std::string subtype = type["subtype"].GetString();
  return ScalarTypeFromName(subtype);
}

std::unique_ptr<Type> Type::TypeFromIdentifier(LibraryLoader* loader,
                                               const rapidjson::Value& type) {
  if (!type.HasMember("identifier")) {
    FX_LOGS_OR_CAPTURE(ERROR) << "Invalid type";
    return std::make_unique<InvalidType>();
  }
  std::string id = type["identifier"].GetString();
  size_t split_index = id.find('/');
  std::string library_name = id.substr(0, split_index);
  Library* library = loader->GetLibraryFromName(library_name);
  if (library == nullptr) {
    FX_LOGS_OR_CAPTURE(ERROR) << "Unknown type for identifier: " << library_name;
    return std::make_unique<InvalidType>();
  }

  bool is_nullable = false;
  if (type.HasMember("nullable")) {
    is_nullable = type["nullable"].GetBool();
  }
  return library->TypeFromIdentifier(is_nullable, id);
}

std::unique_ptr<Type> Type::GetType(LibraryLoader* loader, const rapidjson::Value& type) {
  if (!type.HasMember("kind")) {
    FX_LOGS_OR_CAPTURE(ERROR) << "Invalid type";
    return std::make_unique<InvalidType>();
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
    return std::make_unique<ArrayType>(GetType(loader, element_type), element_count);
  }
  if (kind == "vector") {
    const rapidjson::Value& element_type = type["element_type"];
    return std::make_unique<VectorType>(GetType(loader, element_type));
  }
  if (kind == "request") {
    return std::make_unique<HandleType>();
  }
  if (kind == "primitive") {
    return Type::TypeFromPrimitive(type);
  }
  if (kind == "identifier") {
    return Type::TypeFromIdentifier(loader, type);
  }
  FX_LOGS_OR_CAPTURE(ERROR) << "Invalid type " << kind;
  return std::make_unique<InvalidType>();
}

}  // namespace fidl_codec
