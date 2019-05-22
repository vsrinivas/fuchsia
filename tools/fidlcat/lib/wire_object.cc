// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_object.h"

#include <src/lib/fxl/logging.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_types.h"

namespace fidlcat {

const Colors WithoutColors("", "", "", "", "");
const Colors WithColors(/*reset=*/"\u001b[0m", /*red=*/"\u001b[31m",
                        /*green=*/"\u001b[32m", /*blue=*/"\u001b[34m",
                        /*white_on_magenta=*/"\u001b[45m\u001b[37m");

void Field::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                           rapidjson::Value& result) const {
  std::stringstream ss;
  PrettyPrint(ss, WithoutColors, 0, 0, 0);
  result.SetString(ss.str(), allocator);
}

bool NullableField::DecodeNullable(MessageDecoder* decoder, uint64_t offset) {
  uintptr_t data;
  if (!decoder->GetValueAt(offset, &data)) {
    return false;
  }

  if (data == FIDL_ALLOC_ABSENT) {
    is_null_ = true;
    return true;
  }
  if (data != FIDL_ALLOC_PRESENT) {
    FXL_LOG(ERROR) << "invalid value <" << std::hex << data << std::dec
                   << "> for nullable";
    return false;
  }
  decoder->AddSecondaryObject(this);
  return true;
}

void InlineField::DecodeContent(MessageDecoder* decoder) {
  FXL_LOG(FATAL) << "Field is defined inline";
}

int RawField::DisplaySize(int remaining_size) const { return size_ * 3 - 1; }

void RawField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                           int remaining_size, int max_line_size) const {
  if (size_ == 0) {
    return;
  }
  size_t buffer_size = size_ * 3;
  char buffer[buffer_size];
  for (size_t i = 0; i < size_; ++i) {
    if (i != 0) {
      buffer[i * 3 - 1] = ' ';
    }
    snprintf(buffer + (i * 3), 4, "%02x", data()[i]);
  }
  os << buffer;
}

int StringField::DisplaySize(int remaining_size) const {
  if (is_null()) {
    return 4;
  }
  if (data_ == nullptr) {
    return 7;
  }
  return string_length_ + 2;  // The two quotes.
}

void StringField::DecodeContent(MessageDecoder* decoder) {
  data_ = decoder->GetAddress(0, string_length_);
  decoder->GotoNextObjectOffset(string_length_);
}

void StringField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                              rapidjson::Value& result) const {
  if (is_null()) {
    result.SetNull();
  } else if (data_ == nullptr) {
    result.SetString("(invalid)", allocator);
  } else {
    result.SetString(reinterpret_cast<const char*>(data_), string_length_,
                     allocator);
  }
}

void StringField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                              int remaining_size, int max_line_size) const {
  os << colors.red;
  if (is_null()) {
    os << "null";
  } else if (data_ == nullptr) {
    os << "invalid";
  } else {
    os << '"'
       << std::string_view(reinterpret_cast<const char*>(data_), string_length_)
       << '"';
  }
  os << colors.reset;
}

int BoolField::DisplaySize(int remaining_size) const { return *data() ? 4 : 5; }

void BoolField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                            int remaining_size, int max_line_size) const {
  if (data() == nullptr) {
    os << colors.red << "invalid" << colors.reset;
  } else {
    os << colors.blue << (*data() ? "true" : "false") << colors.reset;
  }
}

int Object::DisplaySize(int remaining_size) const {
  if (is_null()) {
    return 4;
  }
  int size = 0;
  for (const auto& field : fields_) {
    // Two characters for the separator ("{ " or ", ") and three characters for
    // equal (" = ").
    size += field->name().size() + 5;
    if (field->type() != nullptr) {
      // Two characters for ": ".
      size += field->type()->Name().size() + 2;
    }
    size += field->DisplaySize(remaining_size - size);
    if (size > remaining_size) {
      return size;
    }
  }
  // Two characters for the closing brace (" }").
  size += 2;
  return size;
}

void Object::DecodeContent(MessageDecoder* decoder) {
  DecodeAt(decoder, 0);
  decoder->GotoNextObjectOffset(struct_definition_.size());
}

void Object::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  for (const auto& member : struct_definition_.members()) {
    std::unique_ptr<Field> field = member->type()->Decode(
        decoder, member->name(), base_offset + member->offset());
    if (field != nullptr) {
      fields_.push_back(std::move(field));
    }
  }
}

void Object::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                         rapidjson::Value& result) const {
  if (is_null()) {
    result.SetNull();
  } else {
    result.SetObject();
    for (const auto& field : fields_) {
      rapidjson::Value key;
      key.SetString(field->name().c_str(), allocator);
      result.AddMember(key, rapidjson::Value(), allocator);
      field->ExtractJson(allocator, result[field->name().c_str()]);
    }
  }
}

void Object::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                         int remaining_size, int max_line_size) const {
  if (is_null()) {
    os << colors.blue << "null" << colors.reset;
  } else if (fields_.empty()) {
    os << "{}";
  } else if (DisplaySize(remaining_size) <= remaining_size) {
    const char* separator = "{ ";
    for (const auto& field : fields_) {
      os << separator << field->name();
      separator = ", ";
      if (field->type() != nullptr) {
        std::string type_name = field->type()->Name();
        os << ": " << colors.green << type_name << colors.reset;
      }
      os << " = ";
      field->PrettyPrint(os, colors, tabs + 1, max_line_size, max_line_size);
    }
    os << " }";
  } else {
    os << "{\n";
    for (const auto& field : fields_) {
      int size = (tabs + 1) * kTabSize + field->name().size();
      os << std::string((tabs + 1) * kTabSize, ' ') << field->name();
      if (field->type() != nullptr) {
        std::string type_name = field->type()->Name();
        // Two characters for ": ".
        size += type_name.size() + 2;
        os << ": " << colors.green << type_name << colors.reset;
      }
      size += 3;
      os << " = ";
      field->PrettyPrint(os, colors, tabs + 1, max_line_size - size,
                         max_line_size);
      os << "\n";
    }
    os << std::string(tabs * kTabSize, ' ') << '}';
  }
}

EnvelopeField::EnvelopeField(std::string_view name, const Type* type)
    : NullableField(name, type) {}

int EnvelopeField::DisplaySize(int remaining_size) const {
  return field_->DisplaySize(remaining_size);
}

void EnvelopeField::DecodeContent(MessageDecoder* decoder) {
  MessageDecoder envelope_decoder(decoder, num_bytes_, num_handles_);
  field_ = envelope_decoder.DecodeField(name(), type());
  decoder->GotoNextObjectOffset(num_bytes_);
  decoder->SkipHandles(num_handles_);
}

void EnvelopeField::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  decoder->GetValueAt(base_offset, &num_bytes_);
  base_offset += sizeof(num_bytes_);
  decoder->GetValueAt(base_offset, &num_handles_);
  base_offset += sizeof(num_handles_);

  if (DecodeNullable(decoder, base_offset)) {
    if (type() == nullptr) {
      FXL_DCHECK(is_null());
    }
    if (is_null()) {
      FXL_DCHECK(num_bytes_ == 0);
      FXL_DCHECK(num_handles_ == 0);
      return;
    }
  }
}

void EnvelopeField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                                rapidjson::Value& result) const {
  field_->ExtractJson(allocator, result);
}

void EnvelopeField::PrettyPrint(std::ostream& os, const Colors& colors,
                                int tabs, int remaining_size,
                                int max_line_size) const {
  field_->PrettyPrint(os, colors, tabs, remaining_size, max_line_size);
}

TableField::TableField(std::string_view name, const Type* type,
                       const Table& table_definition, uint64_t envelope_count)
    : NullableField(name, type),
      table_definition_(table_definition),
      envelope_count_(envelope_count) {}

int TableField::DisplaySize(int remaining_size) const {
  int size = 0;
  for (const auto& envelope : envelopes_) {
    if (!envelope->is_null()) {
      // Two characters for the separator ("{ " or ", ") and three characters
      // for equal (" = ").
      size += envelope->name().size() + 5;
      if (envelope->type() != nullptr) {
        size += envelope->type()->Name().size() + 2;
      }
      size += envelope->DisplaySize(remaining_size - size);
      if (size > remaining_size) {
        return size;
      }
    }
  }
  // Two characters for the closing brace (" }").
  size += 2;
  return size;
}

void TableField::DecodeContent(MessageDecoder* decoder) {
  uint64_t offset = 0;
  for (uint64_t envelope_id = 0; envelope_id < envelope_count_; ++envelope_id) {
    const TableMember* member =
        (envelope_id < table_definition_.members().size() - 1)
            ? table_definition_.members()[envelope_id + 1]
            : nullptr;
    std::unique_ptr<EnvelopeField> envelope;
    if (member == nullptr) {
      std::string key_name =
          std::string("unknown$") + std::to_string(envelope_id + 1);
      envelope = std::make_unique<EnvelopeField>(
          key_name, table_definition_.unknown_member_type());
    } else {
      envelope =
          std::make_unique<EnvelopeField>(member->name(), member->type());
    }
    envelope->DecodeAt(decoder, offset);
    envelopes_.push_back(std::move(envelope));
    offset += 2 * sizeof(uint64_t);
  }
  decoder->GotoNextObjectOffset(offset);
}

void TableField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                             rapidjson::Value& result) const {
  result.SetObject();
  for (const auto& envelope : envelopes_) {
    if (!envelope->is_null()) {
      rapidjson::Value key;
      key.SetString(envelope->name().c_str(), allocator);
      result.AddMember(key, rapidjson::Value(), allocator);
      envelope->ExtractJson(allocator, result[envelope->name().c_str()]);
    }
  }
}

void TableField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                             int remaining_size, int max_line_size) const {
  int display_size = DisplaySize(remaining_size);
  if (display_size == 2) {
    os << "{}";
  } else if (DisplaySize(remaining_size) <= remaining_size) {
    const char* separator = "{ ";
    for (const auto& envelope : envelopes_) {
      if (!envelope->is_null()) {
        os << separator << envelope->name();
        separator = ", ";
        if (envelope->type() != nullptr) {
          std::string type_name = envelope->type()->Name();
          os << ": " << colors.green << type_name << colors.reset;
        }
        os << " = ";
        envelope->PrettyPrint(os, colors, tabs + 1, max_line_size,
                              max_line_size);
      }
    }
    os << " }";
  } else {
    os << "{\n";
    for (const auto& envelope : envelopes_) {
      if (!envelope->is_null()) {
        int size = (tabs + 1) * kTabSize + envelope->name().size() + 3;
        os << std::string((tabs + 1) * kTabSize, ' ') << envelope->name();
        if (envelope->type() != nullptr) {
          std::string type_name = envelope->type()->Name();
          size += type_name.size() + 2;
          os << ": " << colors.green << type_name << colors.reset;
        }
        os << " = ";
        envelope->PrettyPrint(os, colors, tabs + 1, max_line_size - size,
                              max_line_size);
        os << "\n";
      }
    }
    os << std::string(tabs * kTabSize, ' ') << '}';
  }
}

int UnionField::DisplaySize(int remaining_size) const {
  if (is_null()) {
    return 4;
  }
  // Two characters for the opening brace ("{ ") + three characters for equal
  // (" = ") and two characters for the closing brace (" }").
  int size = field_->name().size() + 7;
  if (field_->type() != nullptr) {
    // Two characters for ": ".
    size += field_->type()->Name().size() + 2;
  }
  size += field_->DisplaySize(remaining_size - size);
  return size;
}

void UnionField::DecodeContent(MessageDecoder* decoder) {
  DecodeAt(decoder, 0);
  decoder->GotoNextObjectOffset(union_definition_.size());
}

void UnionField::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  uint32_t tag = 0;
  decoder->GetValueAt(base_offset, &tag);
  const UnionMember* member = union_definition_.MemberWithTag(tag);
  if (member == nullptr) {
    field_ = std::make_unique<RawField>(
        std::string("unknown$") + std::to_string(tag), nullptr, nullptr, 0);
  } else {
    field_ = member->type()->Decode(decoder, member->name(),
                                    base_offset + member->offset());
  }
}

void UnionField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                             rapidjson::Value& result) const {
  if (is_null()) {
    result.SetNull();
  } else {
    result.SetObject();
    rapidjson::Value key;
    key.SetString(field_->name().c_str(), allocator);
    result.AddMember(key, rapidjson::Value(), allocator);
    field_->ExtractJson(allocator, result[field_->name().c_str()]);
  }
}

void UnionField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                             int remaining_size, int max_line_size) const {
  if (is_null()) {
    os << colors.blue << "null" << colors.reset;
  } else if (DisplaySize(remaining_size) <= remaining_size) {
    // Two characters for the opening brace ("{ ") + three characters for equal
    // (" = ") and two characters for the closing brace (" }").
    int size = field_->name().size() + 7;
    os << "{ " << field_->name();
    if (field_->type() != nullptr) {
      std::string type_name = field_->type()->Name();
      // Two characters for ": ".
      size += type_name.size() + 2;
      os << ": " << colors.green << type_name << colors.reset;
    }
    os << " = ";
    field_->PrettyPrint(os, colors, tabs + 1, max_line_size - size,
                        max_line_size);
    os << " }";
  } else {
    os << "{\n";
    // Three characters for " = ".
    int size = (tabs + 1) * kTabSize + field_->name().size() + 3;
    os << std::string((tabs + 1) * kTabSize, ' ') << field_->name();
    if (field_->type() != nullptr) {
      std::string type_name = field_->type()->Name();
      // Two characters for ": ".
      size += type_name.size() + 2;
      os << ": " << colors.green << type_name << colors.reset;
    }
    os << " = ";
    field_->PrettyPrint(os, colors, tabs + 1, max_line_size - size,
                        max_line_size);
    os << '\n';
    os << std::string(tabs * kTabSize, ' ') << "}";
  }
}

int ArrayField::DisplaySize(int remaining_size) const {
  int size = 2;
  for (const auto& field : fields_) {
    // Two characters for ", ".
    size += field->DisplaySize(remaining_size - size) + 2;
    if (size > remaining_size) {
      return size;
    }
  }
  return size;
}

void ArrayField::DecodeContent(MessageDecoder* decoder) {
  FXL_LOG(FATAL) << "Field is defined inline";
}

void ArrayField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                             rapidjson::Value& result) const {
  result.SetArray();
  for (const auto& field : fields_) {
    rapidjson::Value element;
    field->ExtractJson(allocator, element);
    result.PushBack(element, allocator);
  }
}

void ArrayField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                             int remaining_size, int max_line_size) const {
  if (fields_.empty()) {
    os << "[]";
  } else if (DisplaySize(remaining_size) <= remaining_size) {
    const char* separator = "[ ";
    for (const auto& field : fields_) {
      os << separator;
      separator = ", ";
      field->PrettyPrint(os, colors, tabs + 1, max_line_size, max_line_size);
    }
    os << " ]";
  } else {
    os << "[\n";
    for (const auto& field : fields_) {
      int size = (tabs + 1) * kTabSize;
      os << std::string((tabs + 1) * kTabSize, ' ');
      field->PrettyPrint(os, colors, tabs + 1, max_line_size - size,
                         max_line_size);
      os << "\n";
    }
    os << std::string(tabs * kTabSize, ' ') << ']';
  }
}

int VectorField::DisplaySize(int remaining_size) const {
  if (is_null()) {
    return 4;
  }
  int size = 0;
  for (const auto& field : fields_) {
    // Two characters for the separator ("[ " or ", ").
    size += field->DisplaySize(remaining_size - size) + 2;
    if (size > remaining_size) {
      return size;
    }
  }
  // Two characters for the closing bracket (" ]").
  size += 2;
  return size;
}

void VectorField::DecodeContent(MessageDecoder* decoder) {
  uint64_t offset = 0;
  for (uint64_t i = 0; i < size_; ++i) {
    std::unique_ptr<Field> field = component_type_->Decode(decoder, "", offset);
    if (field != nullptr) {
      fields_.push_back(std::move(field));
    }
    offset += component_type_->InlineSize();
  }
  decoder->GotoNextObjectOffset(offset);
}

void VectorField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                              rapidjson::Value& result) const {
  if (is_null()) {
    result.SetNull();
  } else {
    result.SetArray();
    for (const auto& field : fields_) {
      rapidjson::Value element;
      field->ExtractJson(allocator, element);
      result.PushBack(element, allocator);
    }
  }
}

void VectorField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                              int remaining_size, int max_line_size) const {
  if (is_null()) {
    os << colors.blue << "null" << colors.reset;
  } else if (fields_.empty()) {
    os << "[]";
  } else if (DisplaySize(remaining_size) <= remaining_size) {
    const char* separator = "[ ";
    for (const auto& field : fields_) {
      os << separator;
      separator = ", ";
      field->PrettyPrint(os, colors, tabs + 1, max_line_size, max_line_size);
    }
    os << " ]";
  } else {
    os << "[\n";
    for (const auto& field : fields_) {
      int size = (tabs + 1) * kTabSize;
      os << std::string((tabs + 1) * kTabSize, ' ');
      field->PrettyPrint(os, colors, tabs + 1, max_line_size - size,
                         max_line_size);
      os << "\n";
    }
    os << std::string(tabs * kTabSize, ' ') << ']';
  }
}

int EnumField::DisplaySize(int remaining_size) const {
  if (data() == nullptr) {
    return 7;
  }
  return enum_definition_.GetNameFromBytes(data()).size();
}

void EnumField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                            rapidjson::Value& result) const {
  if (data() == nullptr) {
    result.SetString("(invalid)", allocator);
  } else {
    std::string name = enum_definition_.GetNameFromBytes(data());
    result.SetString(name.c_str(), allocator);
  }
}

void EnumField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                            int remaining_size, int max_line_size) const {
  if (data() == nullptr) {
    os << colors.red << "invalid" << colors.reset;
  } else {
    os << colors.blue << enum_definition_.GetNameFromBytes(data())
       << colors.reset;
  }
}

int HandleField::DisplaySize(int remaining_size) const {
  return std::to_string(handle_).size();
}

void HandleField::DecodeContent(MessageDecoder* decoder) {
  FXL_LOG(FATAL) << "Handle field is defined inline";
}

void HandleField::PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                              int remaining_size, int max_line_size) const {
  os << colors.red << handle_ << colors.reset;
}

}  // namespace fidlcat
