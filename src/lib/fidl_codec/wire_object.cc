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
constexpr char kNull[] = "null";

const Colors WithoutColors("", "", "", "", "", "");
const Colors WithColors(/*new_reset=*/"\u001b[0m", /*new_red=*/"\u001b[31m",
                        /*new_green=*/"\u001b[32m", /*new_blue=*/"\u001b[34m",
                        /*new_white_on_magenta=*/"\u001b[45m\u001b[37m",
                        /*new_yellow_background=*/"\u001b[103m");

void Value::Visit(Visitor* visitor) const { visitor->VisitValue(this); }

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

void InlineValue::DecodeContent(MessageDecoder* /*decoder*/, uint64_t /*offset*/) {
  FXL_LOG(FATAL) << "Value is defined inline";
}

void InlineValue::Visit(Visitor* visitor) const { visitor->VisitInlineValue(this); }

int RawValue::DisplaySize(int /*remaining_size*/) const { return static_cast<int>(size_) * 3 - 1; }

void RawValue::PrettyPrint(std::ostream& os, const Colors& /*colors*/,
                           const fidl_message_header_t* /*header*/,
                           std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                           int /*max_line_size*/) const {
  if (size_ == 0) {
    return;
  }
  size_t buffer_size = size_ * 3;
  std::vector<char> buffer(buffer_size);
  for (size_t i = 0; i < size_; ++i) {
    if (i != 0) {
      buffer[i * 3 - 1] = ' ';
    }
    snprintf(buffer.data() + (i * 3), 4, "%02x", data()[i]);
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
  if (is_null()) {
    return strlen(kNull);
  }
  if (data_ == nullptr) {
    return strlen(kInvalid);
  }
  return static_cast<int>(string_length_) + 2;  // The two quotes.
}

void StringValue::DecodeContent(MessageDecoder* decoder, uint64_t offset) {
  data_ = decoder->GetAddress(offset, string_length_);
}

void StringValue::PrettyPrint(std::ostream& os, const Colors& colors,
                              const fidl_message_header_t* /*header*/,
                              std::string_view /*line_header*/, int /*tabs*/,
                              int /*remaining_size*/, int /*max_line_size*/) const {
  os << colors.red;
  if (is_null()) {
    os << kNull;
  } else if (data_ == nullptr) {
    os << kInvalid;
  } else {
    os << '"' << std::string_view(reinterpret_cast<const char*>(data_), string_length_) << '"';
  }
  os << colors.reset;
}

void StringValue::Visit(Visitor* visitor) const { visitor->VisitStringValue(this); }

int BoolValue::DisplaySize(int /*remaining_size*/) const {
  constexpr int kTrueSize = 4;
  constexpr int kFalseSize = 5;
  constexpr int kInvalidSize = 7;
  return (data() == nullptr) ? kInvalidSize : (*data() ? kTrueSize : kFalseSize);
}

void BoolValue::PrettyPrint(std::ostream& os, const Colors& colors,
                            const fidl_message_header_t* /*header*/,
                            std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                            int /*max_line_size*/) const {
  if (data() == nullptr) {
    os << colors.red << "invalid" << colors.reset;
  } else {
    os << colors.blue << (*data() ? "true" : "false") << colors.reset;
  }
}

void BoolValue::Visit(Visitor* visitor) const { visitor->VisitBoolValue(this); }

int Object::DisplaySize(int remaining_size) const {
  if (is_null()) {
    return 4;
  }
  int size = 0;
  for (const auto& [name, value] : fields_) {
    // Two characters for the separator ("{ " or ", ") and three characters for
    // equal (" = ").
    constexpr int kExtraSize = 5;
    size += static_cast<int>(name.size()) + kExtraSize;
    if (value->type() != nullptr) {
      // Two characters for ": ".
      size += static_cast<int>(value->type()->Name().size()) + 2;
    }
    size += value->DisplaySize(remaining_size - size);
    if (size > remaining_size) {
      return size;
    }
  }
  // Two characters for the closing brace (" }").
  size += 2;
  return size;
}

void Object::DecodeContent(MessageDecoder* decoder, uint64_t offset) { DecodeAt(decoder, offset); }

void Object::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  for (const auto& member : struct_definition_.members()) {
    std::unique_ptr<Value> value =
        member->type()->Decode(decoder, base_offset + member->Offset(decoder));
    if (value != nullptr) {
      fields_[std::string(member->name())] = std::move(value);
    }
  }
}

void Object::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                         rapidjson::Value& result) const {
  JsonVisitor visitor(&result, &allocator);

  Visit(&visitor);
}

void Object::PrettyPrint(std::ostream& os, const Colors& colors,
                         const fidl_message_header_t* header, std::string_view line_header,
                         int tabs, int remaining_size, int max_line_size) const {
  if (is_null()) {
    os << colors.blue << "null" << colors.reset;
  } else if (fields_.empty()) {
    os << "{}";
  } else if (DisplaySize(remaining_size) + static_cast<int>(line_header.size()) <= remaining_size) {
    const char* separator = "{ ";
    for (const auto& member : struct_definition_.members()) {
      auto it = fields_.find(std::string(member->name()));
      if (it == fields_.end())
        continue;
      const auto& [name, value] = *it;
      os << separator << name;
      separator = ", ";
      if (value->type() != nullptr) {
        std::string type_name = value->type()->Name();
        os << ": " << colors.green << type_name << colors.reset;
      }
      os << " = ";
      value->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size, max_line_size);
    }
    os << " }";
  } else {
    os << "{\n";
    for (const auto& member : struct_definition_.members()) {
      auto it = fields_.find(std::string(member->name()));
      if (it == fields_.end())
        continue;
      const auto& [name, value] = *it;
      int size = (tabs + 1) * kTabSize + static_cast<int>(name.size());
      os << line_header << std::string((tabs + 1) * kTabSize, ' ') << name;
      if (value->type() != nullptr) {
        std::string type_name = value->type()->Name();
        // Two characters for ": ".
        size += static_cast<int>(type_name.size()) + 2;
        os << ": " << colors.green << type_name << colors.reset;
      }
      size += 3;
      os << " = ";
      value->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                         max_line_size);
      os << "\n";
    }
    os << line_header << std::string(tabs * kTabSize, ' ') << '}';
  }
}

void Object::Visit(Visitor* visitor) const { visitor->VisitObject(this); }

EnvelopeValue::EnvelopeValue(const Type* type) : NullableValue(type) {}

int EnvelopeValue::DisplaySize(int remaining_size) const {
  if (is_null() || (value_ == nullptr)) {
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
      if (!is_null()) {
        decoder->AddError() << std::hex << (decoder->absolute_offset() + base_offset) << std::dec
                            << ": The envelope should be null\n";
      }
    }
    if (is_null()) {
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
  if (is_null() || (value_ == nullptr)) {
    os << colors.red << "null" << colors.reset;
  } else {
    value_->PrettyPrint(os, colors, header, line_header, tabs, remaining_size, max_line_size);
  }
}

void EnvelopeValue::Visit(Visitor* visitor) const { visitor->VisitEnvelopeValue(this); }

TableValue::TableValue(const Type* type, const Table& table_definition, uint64_t envelope_count)
    : NullableValue(type), table_definition_(table_definition), envelope_count_(envelope_count) {}

int TableValue::DisplaySize(int remaining_size) const {
  int size = 0;
  for (const auto& field : envelopes_) {
    if (!field.value()->is_null()) {
      // Two characters for the separator ("{ " or ", ") and three characters
      // for equal (" = ").
      constexpr int kExtraSize = 5;
      size += static_cast<int>(field.name().size()) + kExtraSize;
      if (field.value()->type() != nullptr) {
        size += static_cast<int>(field.value()->type()->Name().size()) + 2;
      }
      size += field.value()->DisplaySize(remaining_size - size);
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
    const TableMember* member = (envelope_id < table_definition_.members().size() - 1)
                                    ? table_definition_.members()[envelope_id + 1]
                                    : nullptr;
    std::unique_ptr<EnvelopeValue> envelope;
    std::string key_name;
    if (member == nullptr) {
      key_name = std::string("unknown$") + std::to_string(envelope_id + 1);
      envelope = std::make_unique<EnvelopeValue>(table_definition_.unknown_member_type());
    } else {
      key_name = member->name();
      envelope = std::make_unique<EnvelopeValue>(member->type());
    }
    envelope->DecodeAt(decoder, offset);
    envelopes_.emplace_back(key_name, std::move(envelope));
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
    for (const auto& field : envelopes_) {
      if (!field.value()->is_null()) {
        os << separator << field.name();
        separator = ", ";
        if (field.value()->type() != nullptr) {
          std::string type_name = field.value()->type()->Name();
          os << ": " << colors.green << type_name << colors.reset;
        }
        os << " = ";
        field.value()->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size,
                                   max_line_size);
      }
    }
    os << " }";
  } else {
    os << "{\n";
    for (const auto& field : envelopes_) {
      if (!field.value()->is_null()) {
        int size = (tabs + 1) * kTabSize + static_cast<int>(field.name().size()) + 3;
        os << line_header << std::string((tabs + 1) * kTabSize, ' ') << field.name();
        if (field.value()->type() != nullptr) {
          std::string type_name = field.value()->type()->Name();
          size += static_cast<int>(type_name.size()) + 2;
          os << ": " << colors.green << type_name << colors.reset;
        }
        os << " = ";
        field.value()->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                                   max_line_size);
        os << "\n";
      }
    }
    os << line_header << std::string(tabs * kTabSize, ' ') << '}';
  }
}

void TableValue::Visit(Visitor* visitor) const { visitor->VisitTableValue(this); }

int UnionValue::DisplaySize(int remaining_size) const {
  if (is_null() || field_.value()->is_null()) {
    return 4;
  }
  // Two characters for the opening brace ("{ ") + three characters for equal
  // (" = ") and two characters for the closing brace (" }").
  constexpr int kExtraSize = 7;
  int size = static_cast<int>(field_.name().size()) + kExtraSize;
  if (field_.value() != nullptr) {
    if (field_.value()->type() != nullptr) {
      // Two characters for ": ".
      size += static_cast<int>(field_.value()->type()->Name().size()) + 2;
    }
    size += field_.value()->DisplaySize(remaining_size - size);
  }
  return size;
}

void UnionValue::DecodeContent(MessageDecoder* decoder, uint64_t offset) {
  DecodeAt(decoder, offset);
}

void UnionValue::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  uint32_t tag = 0;
  decoder->GetValueAt(base_offset, &tag);
  const UnionMember* member = union_definition_.MemberWithTag(tag);
  if (member == nullptr) {
    field_ =
        Field("unknown$" + std::to_string(tag), std::make_unique<RawValue>(nullptr, nullptr, 0));
  } else {
    field_ = Field(std::string(member->name()),
                   member->type()->Decode(decoder, base_offset + member->offset()));
  }
}

void UnionValue::PrettyPrint(std::ostream& os, const Colors& colors,
                             const fidl_message_header_t* header, std::string_view line_header,
                             int tabs, int remaining_size, int max_line_size) const {
  if (header != nullptr) {
    os << (fidl_should_decode_union_from_xunion(header) ? "v1!" : "v0!");
  }
  if (is_null() || field_.value()->is_null()) {
    os << colors.blue << "null" << colors.reset;
  } else if (DisplaySize(remaining_size) + static_cast<int>(line_header.size()) <= remaining_size) {
    // Two characters for the opening brace ("{ ") + three characters for equal
    // (" = ") and two characters for the closing brace (" }").
    constexpr int kExtraSize = 7;
    int size = static_cast<int>(field_.name().size()) + kExtraSize;
    os << "{ " << field_.name();
    if (field_.value() != nullptr) {
      if (field_.value()->type() != nullptr) {
        std::string type_name = field_.value()->type()->Name();
        // Two characters for ": ".
        size += static_cast<int>(type_name.size()) + 2;
        os << ": " << colors.green << type_name << colors.reset;
      }
      os << " = ";
      field_.value()->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                                  max_line_size);
    }
    os << " }";
  } else {
    os << "{\n";
    // Three characters for " = ".
    int size = (tabs + 1) * kTabSize + static_cast<int>(field_.name().size()) + 3;
    os << line_header << std::string((tabs + 1) * kTabSize, ' ') << field_.name();
    if (field_.value() != nullptr) {
      if (field_.value()->type() != nullptr) {
        std::string type_name = field_.value()->type()->Name();
        // Two characters for ": ".
        size += static_cast<int>(type_name.size()) + 2;
        os << ": " << colors.green << type_name << colors.reset;
      }
      os << " = ";
      field_.value()->PrettyPrint(os, colors, header, line_header, tabs + 1, max_line_size - size,
                                  max_line_size);
    }
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

void ArrayValue::DecodeContent(MessageDecoder* /*decoder*/, uint64_t /*offset*/) {
  FXL_LOG(FATAL) << "Value is defined inline";
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
  if (is_null()) {
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
  for (uint64_t i = 0;
       (i < size_) && (offset + component_type_->InlineSize(decoder) <= decoder->num_bytes());
       ++i) {
    std::unique_ptr<Value> value = component_type_->Decode(decoder, offset);
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
    offset += component_type_->InlineSize(decoder);
  }
}

void VectorValue::PrettyPrint(std::ostream& os, const Colors& colors,
                              const fidl_message_header_t* header, std::string_view line_header,
                              int tabs, int remaining_size, int max_line_size) const {
  if (is_null()) {
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
  if (data() == nullptr) {
    return strlen(kInvalid);
  }
  return enum_definition_.GetNameFromBytes(data()).size();
}

void EnumValue::PrettyPrint(std::ostream& os, const Colors& colors,
                            const fidl_message_header_t* /*header*/,
                            std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                            int /*max_line_size*/) const {
  if (data() == nullptr) {
    os << colors.red << kInvalid << colors.reset;
  } else {
    os << colors.blue << enum_definition_.GetNameFromBytes(data()) << colors.reset;
  }
}

void EnumValue::Visit(Visitor* visitor) const { visitor->VisitEnumValue(this); }

int BitsValue::DisplaySize(int /*remaining_size*/) const {
  if (data() == nullptr) {
    return strlen(kInvalid);
  }
  return bits_definition_.GetNameFromBytes(data()).size();
}

void BitsValue::PrettyPrint(std::ostream& os, const Colors& colors,
                            const fidl_message_header_t* /*header*/,
                            std::string_view /*line_header*/, int /*tabs*/, int /*remaining_size*/,
                            int /*max_line_size*/) const {
  if (data() == nullptr) {
    os << colors.red << kInvalid << colors.reset;
  } else {
    os << colors.blue << bits_definition_.GetNameFromBytes(data()) << colors.reset;
  }
}

void BitsValue::Visit(Visitor* visitor) const { visitor->VisitBitsValue(this); }

int HandleValue::DisplaySize(int /*remaining_size*/) const {
  return std::to_string(handle_.handle).size();
}

void HandleValue::DecodeContent(MessageDecoder* /*decoder*/, uint64_t /*offset*/) {
  FXL_LOG(FATAL) << "Handle value is defined inline";
}

void HandleValue::PrettyPrint(std::ostream& os, const Colors& colors,
                              const fidl_message_header_t* /*header*/,
                              std::string_view /*line_header*/, int /*tabs*/,
                              int /*remaining_size*/, int /*max_line_size*/) const {
  DisplayHandle(colors, handle_, os);
}

void HandleValue::Visit(Visitor* visitor) const { visitor->VisitHandleValue(this); }

}  // namespace fidl_codec
