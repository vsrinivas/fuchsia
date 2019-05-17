// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_object.h"

#include <src/lib/fxl/logging.h>

#include <memory>
#include <vector>

#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_types.h"

namespace fidlcat {

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

void RawField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                           rapidjson::Value& result) const {
  size_t buffer_size = size_ * 3;
  char buffer[buffer_size];
  for (size_t i = 0; i < size_; ++i) {
    if (i != 0) {
      buffer[i * 3 - 1] = ' ';
    }
    snprintf(buffer + (i * 3), 4, "%02x", data()[i]);
  }
  result.SetString(buffer, allocator);
}

void StringField::DecodeContent(MessageDecoder* decoder) {
  data_ = decoder->GetAddress(0, string_length_);
  decoder->GotoNextObjectOffset(string_length_);
}

void StringField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                              rapidjson::Value& result) const {
  if (is_null()) {
    result.SetString("(null)", allocator);
  } else if (data_ == nullptr) {
    result.SetString("(invalid)", allocator);
  } else {
    result.SetString(reinterpret_cast<const char*>(data_), string_length_,
                     allocator);
  }
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

EnvelopeField::EnvelopeField(std::string_view name, const Type* type)
    : NullableField(name), type_(type) {}

void EnvelopeField::DecodeContent(MessageDecoder* decoder) {
  MessageDecoder envelope_decoder(decoder, num_bytes_, num_handles_);
  field_ = envelope_decoder.DecodeField(name(), type_);
  decoder->GotoNextObjectOffset(num_bytes_);
  decoder->SkipHandles(num_handles_);
}

void EnvelopeField::DecodeAt(MessageDecoder* decoder, uint64_t base_offset) {
  decoder->GetValueAt(base_offset, &num_bytes_);
  base_offset += sizeof(num_bytes_);
  decoder->GetValueAt(base_offset, &num_handles_);
  base_offset += sizeof(num_handles_);

  if (DecodeNullable(decoder, base_offset)) {
    if (type_ == nullptr) {
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

TableField::TableField(std::string_view name, const Table& table_definition,
                       uint64_t envelope_count)
    : NullableField(name),
      table_definition_(table_definition),
      envelope_count_(envelope_count) {}

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
        std::string("unknown$") + std::to_string(tag), nullptr, 0);
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

void EnumField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                            rapidjson::Value& result) const {
  if (data() == nullptr) {
    result.SetString("(invalid)", allocator);
  } else {
    std::string name = enum_definition_.GetNameFromBytes(data());
    result.SetString(name.c_str(), allocator);
  }
}

void HandleField::DecodeContent(MessageDecoder* decoder) {
  FXL_LOG(FATAL) << "Handle field is defined inline";
}

void HandleField::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                              rapidjson::Value& result) const {
  result.SetString(std::to_string(handle_), allocator);
}

}  // namespace fidlcat
