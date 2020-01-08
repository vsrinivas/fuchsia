// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_object.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#include "src/lib/fidl_codec/colors.h"
#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fidl_codec/json_visitor.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/visitor.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "src/lib/fxl/logging.h"

namespace fidl_codec {

constexpr char kInvalid[] = "invalid";

const Colors WithoutColors("", "", "", "", "", "");
const Colors WithColors(/*new_reset=*/"\u001b[0m", /*new_red=*/"\u001b[31m",
                        /*new_green=*/"\u001b[32m", /*new_blue=*/"\u001b[34m",
                        /*new_white_on_magenta=*/"\u001b[45m\u001b[37m",
                        /*new_yellow_background=*/"\u001b[103m");

void Value::Visit(Visitor* visitor) const { visitor->VisitValue(this); }

void InvalidValue::Visit(Visitor* visitor) const { visitor->VisitInvalidValue(this); }

void NullValue::Visit(Visitor* visitor) const { visitor->VisitNullValue(this); }

bool NullableValue::DecodeNullable(MessageDecoder* decoder, uint64_t offset, uint64_t size) {
  uintptr_t data;
  if (!decoder->GetValueAt(offset, &data)) {
    return false;
  }

  if (data == FIDL_ALLOC_ABSENT) {
    is_null_ = true;
    return true;
  }
  if (data != FIDL_ALLOC_PRESENT) {
    if (type() == nullptr) {
      decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                          << ": Invalid value <" << std::hex << data << std::dec
                          << "> for nullable\n";
    } else {
      decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                          << ": Invalid value <" << std::hex << data << std::dec
                          << "> for nullable " << type()->Name() << "\n";
    }
    return false;
  }
  uint64_t nullable_offset = decoder->next_object_offset();
  // Set the offset for the next object (just after this one).
  decoder->SkipObject(size);
  // Decode the object.
  DecodeContent(decoder, nullable_offset);
  return true;
}

void NullableValue::Visit(Visitor* visitor) const { visitor->VisitNullableValue(this); }

int RawValue::DisplaySize(int /*remaining_size*/) const {
  return (data_.size() == 0) ? 0 : static_cast<int>(data_.size()) * 3 - 1;
}

void RawValue::PrettyPrint(std::ostream& os, const Colors& /*colors*/,
                           const fidl_message_header_t* /*header*/,
                           std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                           int /*max_line_size*/) const {
  if (data_.size() == 0) {
    return;
  }
  size_t buffer_size = data_.size() * 3;
  std::vector<char> buffer(buffer_size);
  for (size_t i = 0; i < data_.size(); ++i) {
    if (i != 0) {
      buffer[i * 3 - 1] = ' ';
    }
    snprintf(buffer.data() + (i * 3), 4, "%02x", data_[i]);
  }
  os << buffer.data();
}

void RawValue::Visit(Visitor* visitor) const { visitor->VisitRawValue(this); }

template <>
void NumericValue<uint8_t>::Visit(Visitor* visitor) const {
  visitor->VisitU8Value(this);
}
template <>
void NumericValue<uint16_t>::Visit(Visitor* visitor) const {
  visitor->VisitU16Value(this);
}
template <>
void NumericValue<uint32_t>::Visit(Visitor* visitor) const {
  visitor->VisitU32Value(this);
}
template <>
void NumericValue<uint64_t>::Visit(Visitor* visitor) const {
  visitor->VisitU64Value(this);
}
template <>
void NumericValue<int8_t>::Visit(Visitor* visitor) const {
  visitor->VisitI8Value(this);
}
template <>
void NumericValue<int16_t>::Visit(Visitor* visitor) const {
  visitor->VisitI16Value(this);
}
template <>
void NumericValue<int32_t>::Visit(Visitor* visitor) const {
  visitor->VisitI32Value(this);
}
template <>
void NumericValue<int64_t>::Visit(Visitor* visitor) const {
  visitor->VisitI64Value(this);
}
template <>
void NumericValue<float>::Visit(Visitor* visitor) const {
  visitor->VisitF32Value(this);
}
template <>
void NumericValue<double>::Visit(Visitor* visitor) const {
  visitor->VisitF64Value(this);
}

int StringValue::DisplaySize(int /*remaining_size*/) const {
  return static_cast<int>(string_.size()) + 2;  // The two quotes.
}

void StringValue::PrettyPrint(std::ostream& os, const Colors& colors,
                              const fidl_message_header_t* /*header*/,
                              std::string_view /*line_header*/, int /*tabs*/,
                              int /*remaining_size*/, int /*max_line_size*/) const {
  os << colors.red << '"' << string_ << '"' << colors.reset;
}

void StringValue::Visit(Visitor* visitor) const { visitor->VisitStringValue(this); }

int BoolValue::DisplaySize(int /*remaining_size*/) const {
  constexpr int kTrueSize = 4;
  constexpr int kFalseSize = 5;
  return value_ ? kTrueSize : kFalseSize;
}

void BoolValue::PrettyPrint(std::ostream& os, const Colors& colors,
                            const fidl_message_header_t* /*header*/,
                            std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                            int /*max_line_size*/) const {
  os << colors.blue << (value_ ? "true" : "false") << colors.reset;
}

void BoolValue::Visit(Visitor* visitor) const { visitor->VisitBoolValue(this); }

int StructValue::DisplaySize(int remaining_size) const {
  if (IsNull()) {
    return 4;
  }
  int size = 0;
  for (const auto& member : struct_definition_.members()) {
    auto it = fields_.find(member.get());
    if (it == fields_.end())
      continue;
    // Two characters for the separator ("{ " or ", ") and three characters for
    // equal (" = ").
    constexpr int kExtraSize = 5;
    size += static_cast<int>(member->name().size()) + kExtraSize;
    // Two characters for ": ".
    size += static_cast<int>(member->type()->Name().size()) + 2;
    size += it->second->DisplaySize(remaining_size - size);
    if (size > remaining_size) {
      return size;
    }
  }
  // Two characters for the closing brace (" }").
  size += 2;
  return size;
}

void StructValue::DecodeContent(MessageDecoder* decoder, uint64_t offset) {
  DecodeAt(decoder, offset);
}

void StructValue::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  for (const auto& member : struct_definition_.members()) {
    std::unique_ptr<Value> value =
        member->type()->Decode(decoder, base_offset + member->Offset(decoder));
    if (value != nullptr) {
      fields_.emplace(std::make_pair(member.get(), std::move(value)));
    }
  }
}

void StructValue::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                              rapidjson::Value& result) const {
  JsonVisitor visitor(&result, &allocator);

  Visit(&visitor);
}

void StructValue::PrettyPrint(std::ostream& os, const Colors& colors,
                              const fidl_message_header_t* header, std::string_view line_header,
                              int tabs, int remaining_size, int max_line_size) const {
  if (IsNull()) {
    os << colors.blue << "null" << colors.reset;
  } else if (fields_.empty()) {
    os << "{}";
  } else if (DisplaySize(remaining_size) + static_cast<int>(line_header.size()) <= remaining_size) {
    const char* separator = "{ ";
    for (const auto& member : struct_definition_.members()) {
      auto it = fields_.find(member.get());
      if (it == fields_.end())
        continue;
      os << separator << member->name() << ": " << colors.green << member->type()->Name()
         << colors.reset << " = ";
      it->second->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size,
                              max_line_size);
      separator = ", ";
    }
    os << " }";
  } else {
    os << "{\n";
    for (const auto& member : struct_definition_.members()) {
      auto it = fields_.find(member.get());
      if (it == fields_.end())
        continue;
      int size = (tabs + 1) * kTabSize + static_cast<int>(member->name().size());
      os << line_header << std::string((tabs + 1) * kTabSize, ' ') << member->name();
      std::string type_name = member->type()->Name();
      // Two characters for ": ", three characters for " = ".
      os << ": " << colors.green << type_name << colors.reset << " = ";
      size += static_cast<int>(type_name.size()) + 2 + 3;
      it->second->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                              max_line_size);
      os << "\n";
    }
    os << line_header << std::string(tabs * kTabSize, ' ') << '}';
  }
}

void StructValue::Visit(Visitor* visitor) const { visitor->VisitStructValue(this); }

EnvelopeValue::EnvelopeValue(const Type* type) : NullableValue(type) {}

int EnvelopeValue::DisplaySize(int remaining_size) const {
  if (IsNull() || (value_ == nullptr)) {
    return 4;
  }
  return value_->DisplaySize(remaining_size);
}

void EnvelopeValue::DecodeContent(MessageDecoder* decoder, uint64_t offset) {
  if (offset + num_bytes_ > decoder->num_bytes()) {
    decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                        << ": Not enough data to decode an envelope\n";
    return;
  }
  if (num_handles_ > decoder->GetRemainingHandles()) {
    decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                        << ": Not enough handles to decode an envelope\n";
    return;
  }
  MessageDecoder envelope_decoder(decoder, offset, num_bytes_, num_handles_);
  value_ = envelope_decoder.DecodeValue(type());
}

void EnvelopeValue::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  decoder->GetValueAt(base_offset, &num_bytes_);
  base_offset += sizeof(num_bytes_);
  decoder->GetValueAt(base_offset, &num_handles_);
  base_offset += sizeof(num_handles_);

  if (DecodeNullable(decoder, base_offset, num_bytes_)) {
    if (type() == nullptr) {
      if (!IsNull()) {
        decoder->AddError() << std::hex << (decoder->absolute_offset() + base_offset) << std::dec
                            << ": The envelope should be null\n";
      }
    }
    if (IsNull()) {
      if (num_bytes_ != 0) {
        decoder->AddError() << std::hex << (decoder->absolute_offset() + base_offset) << std::dec
                            << ": Null envelope shouldn't have bytes\n";
      }
      if (num_handles_ != 0) {
        decoder->AddError() << std::hex << (decoder->absolute_offset() + base_offset) << std::dec
                            << ": Null envelope shouldn't have handles\n";
      }
    }
  }
}

void EnvelopeValue::PrettyPrint(std::ostream& os, const Colors& colors,
                                const fidl_message_header_t* header, std::string_view line_header,
                                int tabs, int remaining_size, int max_line_size) const {
  if (IsNull() || (value_ == nullptr)) {
    os << colors.red << "null" << colors.reset;
  } else {
    value_->PrettyPrint(os, colors, header, line_header, tabs, remaining_size, max_line_size);
  }
}

void EnvelopeValue::Visit(Visitor* visitor) const { visitor->VisitEnvelopeValue(this); }

TableValue::TableValue(const Type* type, const Table& table_definition, uint64_t envelope_count)
    : NullableValue(type), table_definition_(table_definition), envelope_count_(envelope_count) {}

bool TableValue::AddMember(std::string_view name, std::unique_ptr<Value> value) {
  const TableMember* member = table_definition_.GetMember(name);
  if (member == nullptr) {
    return false;
  }
  AddMember(member, std::move(value));
  return true;
}

int TableValue::DisplaySize(int remaining_size) const {
  int size = 0;
  for (const auto& member : table_definition_.members()) {
    if ((member != nullptr) && !member->reserved()) {
      auto it = members_.find(member.get());
      if ((it == members_.end()) || it->second->IsNull())
        continue;
      // Two characters for the separator ("{ " or ", "), three characters for " = ".
      size += static_cast<int>(member->name().size()) + 2 + 3;
      // Two characters for ": ".
      size += static_cast<int>(member->type()->Name().size()) + 2;
      size += it->second->DisplaySize(remaining_size - size);
      if (size > remaining_size) {
        return size;
      }
    }
  }
  // Two characters for the closing brace (" }").
  size += 2;
  return size;
}

void TableValue::DecodeContent(MessageDecoder* decoder, uint64_t offset) {
  for (uint64_t envelope_id = 0; envelope_id < envelope_count_; ++envelope_id) {
    const TableMember* member = table_definition_.GetMember(envelope_id + 1);
    std::unique_ptr<EnvelopeValue> envelope = std::make_unique<EnvelopeValue>(
        (member == nullptr) ? table_definition_.unknown_member_type() : member->type());
    envelope->DecodeAt(decoder, offset);
    AddMember(member, std::move(envelope));
    offset += 2 * sizeof(uint64_t);
  }
}

void TableValue::PrettyPrint(std::ostream& os, const Colors& colors,
                             const fidl_message_header_t* header, std::string_view line_header,
                             int tabs, int remaining_size, int max_line_size) const {
  int display_size = DisplaySize(remaining_size);
  if (display_size == 2) {
    os << "{}";
  } else if (DisplaySize(remaining_size) + static_cast<int>(line_header.size()) <= remaining_size) {
    const char* separator = "{ ";
    for (const auto& member : table_definition_.members()) {
      if ((member != nullptr) && !member->reserved()) {
        auto it = members_.find(member.get());
        if ((it == members_.end()) || it->second->IsNull())
          continue;
        os << separator << member->name() << ": " << colors.green << member->type()->Name()
           << colors.reset << " = ";
        separator = ", ";
        it->second->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size,
                                max_line_size);
      }
    }
    os << " }";
  } else {
    os << "{\n";
    for (const auto& member : table_definition_.members()) {
      if ((member != nullptr) && !member->reserved()) {
        auto it = members_.find(member.get());
        if ((it == members_.end()) || it->second->IsNull())
          continue;
        int size = (tabs + 1) * kTabSize + static_cast<int>(member->name().size());
        os << line_header << std::string((tabs + 1) * kTabSize, ' ') << member->name();
        std::string type_name = member->type()->Name();
        // Two characters for ": ", three characters for " = ".
        size += static_cast<int>(type_name.size()) + 2 + 3;
        os << ": " << colors.green << type_name << colors.reset << " = ";
        it->second->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                                max_line_size);
        os << "\n";
      }
    }
    os << line_header << std::string(tabs * kTabSize, ' ') << '}';
  }
}

void TableValue::Visit(Visitor* visitor) const { visitor->VisitTableValue(this); }

int UnionValue::DisplaySize(int remaining_size) const {
  if (IsNull() || value_->IsNull()) {
    return 4;
  }
  // Two characters for the opening brace ("{ ") + three characters for equal
  // (" = ") and two characters for the closing brace (" }").
  constexpr int kExtraSize = 7;
  int size = static_cast<int>(member_->name().size()) + kExtraSize;
  // Two characters for ": ".
  size += static_cast<int>(member_->type()->Name().size()) + 2;
  size += value_->DisplaySize(remaining_size - size);
  return size;
}

void UnionValue::DecodeContent(MessageDecoder* decoder, uint64_t offset) {
  DecodeAt(decoder, offset);
}

void UnionValue::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  uint32_t tag = 0;
  decoder->GetValueAt(base_offset, &tag);
  member_ = union_definition_.MemberWithTag(tag);
  if (member_ == nullptr) {
    value_ = std::make_unique<InvalidValue>(nullptr);
  } else {
    value_ = member_->type()->Decode(decoder, base_offset + member_->offset());
  }
}

void UnionValue::PrettyPrint(std::ostream& os, const Colors& colors,
                             const fidl_message_header_t* header, std::string_view line_header,
                             int tabs, int remaining_size, int max_line_size) const {
  if (header != nullptr) {
    os << (fidl_should_decode_union_from_xunion(header) ? "v1!" : "v0!");
  }
  if (IsNull() || value_->IsNull()) {
    os << colors.blue << "null" << colors.reset;
  } else if (DisplaySize(remaining_size) + static_cast<int>(line_header.size()) <= remaining_size) {
    // Two characters for the opening brace ("{ ") + three characters for equal
    // (" = ") and two characters for the closing brace (" }").
    constexpr int kExtraSize = 7;
    int size = static_cast<int>(member_->name().size()) + kExtraSize;
    os << "{ " << member_->name();
    std::string type_name = member_->type()->Name();
    // Two characters for ": ".
    size += static_cast<int>(type_name.size()) + 2;
    os << ": " << colors.green << type_name << colors.reset << " = ";
    value_->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                        max_line_size);
    os << " }";
  } else {
    os << "{\n";
    // Three characters for " = ".
    int size = (tabs + 1) * kTabSize + static_cast<int>(member_->name().size()) + 3;
    os << line_header << std::string((tabs + 1) * kTabSize, ' ') << member_->name();
    std::string type_name = member_->type()->Name();
    // Two characters for ": ".
    size += static_cast<int>(type_name.size()) + 2;
    os << ": " << colors.green << type_name << colors.reset << " = ";
    value_->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                        max_line_size);
    os << '\n';
    os << line_header << std::string(tabs * kTabSize, ' ') << "}";
  }
}

void UnionValue::Visit(Visitor* visitor) const { visitor->VisitUnionValue(this); }

void XUnionValue::Visit(Visitor* visitor) const { visitor->VisitXUnionValue(this); }

int ArrayValue::DisplaySize(int remaining_size) const {
  int size = 2;
  for (const auto& value : values_) {
    // Two characters for ", ".
    size += value->DisplaySize(remaining_size - size) + 2;
    if (size > remaining_size) {
      return size;
    }
  }
  return size;
}

void ArrayValue::PrettyPrint(std::ostream& os, const Colors& colors,
                             const fidl_message_header_t* header, std::string_view line_header,
                             int tabs, int remaining_size, int max_line_size) const {
  if (values_.empty()) {
    os << "[]";
  } else if (DisplaySize(remaining_size) + static_cast<int>(line_header.size()) <= remaining_size) {
    const char* separator = "[ ";
    for (const auto& value : values_) {
      os << separator;
      separator = ", ";
      value->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size, max_line_size);
    }
    os << " ]";
  } else {
    os << "[\n";
    for (const auto& value : values_) {
      int size = (tabs + 1) * kTabSize;
      os << line_header << std::string((tabs + 1) * kTabSize, ' ');
      value->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                         max_line_size);
      os << "\n";
    }
    os << line_header << std::string(tabs * kTabSize, ' ') << ']';
  }
}

void ArrayValue::Visit(Visitor* visitor) const { visitor->VisitArrayValue(this); }

int VectorValue::DisplaySize(int remaining_size) const {
  if (IsNull()) {
    return 4;
  }
  if (is_string_) {
    return static_cast<int>(size_ + 2);  // The string and the two quotes.
  }
  int size = 0;
  for (const auto& value : values_) {
    // Two characters for the separator ("[ " or ", ").
    size += value->DisplaySize(remaining_size - size) + 2;
    if (size > remaining_size) {
      return size;
    }
  }
  // Two characters for the closing bracket (" ]").
  size += 2;
  return size;
}

void VectorValue::DecodeContent(MessageDecoder* decoder, uint64_t offset) {
  if (size_ == 0) {
    return;
  }
  is_string_ = true;
  const Type* component_type = type()->GetComponentType();
  size_t component_size = component_type->InlineSize(decoder->unions_are_xunions());
  for (uint64_t i = 0; (i < size_) && (offset + component_size <= decoder->num_bytes()); ++i) {
    std::unique_ptr<Value> value = component_type->Decode(decoder, offset);
    if (value != nullptr) {
      uint8_t uvalue = value->GetUint8Value();
      if (!std::isprint(uvalue)) {
        if ((uvalue == '\r') || (uvalue == '\n')) {
          has_new_line_ = true;
        } else {
          is_string_ = false;
        }
      }
      values_.push_back(std::move(value));
    }
    offset += component_size;
  }
}

void VectorValue::PrettyPrint(std::ostream& os, const Colors& colors,
                              const fidl_message_header_t* header, std::string_view line_header,
                              int tabs, int remaining_size, int max_line_size) const {
  if (IsNull()) {
    os << colors.blue << "null" << colors.reset;
  } else if (values_.empty()) {
    os << "[]";
  } else if (is_string_) {
    if (has_new_line_) {
      os << "[\n";
      bool needs_header = true;
      for (const auto& value : values_) {
        if (needs_header) {
          os << line_header << std::string((tabs + 1) * kTabSize, ' ');
          needs_header = false;
        }
        uint8_t uvalue = value->GetUint8Value();
        os << uvalue;
        if (uvalue == '\n') {
          needs_header = true;
        }
      }
      if (!needs_header) {
        os << '\n';
      }
      os << line_header << std::string(tabs * kTabSize, ' ') << ']';
    } else {
      os << '"';
      for (const auto& value : values_) {
        os << value->GetUint8Value();
      }
      os << '"';
    }
  } else if (DisplaySize(remaining_size) + static_cast<int>(line_header.size()) <= remaining_size) {
    const char* separator = "[ ";
    for (const auto& value : values_) {
      os << separator;
      separator = ", ";
      value->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size, max_line_size);
    }
    os << " ]";
  } else {
    os << "[\n";
    int size = 0;
    for (const auto& value : values_) {
      int value_size = value->DisplaySize(max_line_size - size);
      if (size == 0) {
        os << line_header << std::string((tabs + 1) * kTabSize, ' ');
        size = (tabs + 1) * kTabSize;
      } else if (value_size + 3 > max_line_size - size) {
        os << ",\n";
        os << line_header << std::string((tabs + 1) * kTabSize, ' ');
        size = (tabs + 1) * kTabSize;
      } else {
        os << ", ";
        size += 2;
      }
      value->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                         max_line_size);
      size += value_size;
    }
    os << '\n';
    os << line_header << std::string(tabs * kTabSize, ' ') << ']';
  }
}

void VectorValue::Visit(Visitor* visitor) const { visitor->VisitVectorValue(this); }

int EnumValue::DisplaySize(int /*remaining_size*/) const {
  if (!data_) {
    return strlen(kInvalid);
  }
  return enum_definition_.GetNameFromBytes(data_->data()).size();
}

void EnumValue::PrettyPrint(std::ostream& os, const Colors& colors,
                            const fidl_message_header_t* /*header*/,
                            std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                            int /*max_line_size*/) const {
  if (!data_) {
    os << colors.red << kInvalid << colors.reset;
  } else {
    os << colors.blue << enum_definition_.GetNameFromBytes(data_->data()) << colors.reset;
  }
}

void EnumValue::Visit(Visitor* visitor) const { visitor->VisitEnumValue(this); }

int BitsValue::DisplaySize(int /*remaining_size*/) const {
  if (!data_) {
    return strlen(kInvalid);
  }
  return bits_definition_.GetNameFromBytes(data_->data()).size();
}

void BitsValue::PrettyPrint(std::ostream& os, const Colors& colors,
                            const fidl_message_header_t* /*header*/,
                            std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                            int /*max_line_size*/) const {
  if (!data_) {
    os << colors.red << kInvalid << colors.reset;
  } else {
    os << colors.blue << bits_definition_.GetNameFromBytes(data_->data()) << colors.reset;
  }
}

void BitsValue::Visit(Visitor* visitor) const { visitor->VisitBitsValue(this); }

int HandleValue::DisplaySize(int /*remaining_size*/) const {
  return std::to_string(handle_.handle).size();
}

void HandleValue::PrettyPrint(std::ostream& os, const Colors& colors,
                              const fidl_message_header_t* /*header*/,
                              std::string_view /*line_header*/, int /*tabs*/,
                              int /*remaining_size*/, int /*max_line_size*/) const {
  DisplayHandle(colors, handle_, os);
}

void HandleValue::Visit(Visitor* visitor) const { visitor->VisitHandleValue(this); }

}  // namespace fidl_codec
