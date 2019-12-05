// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_types.h"

#include <zircon/fidl.h>

#include "rapidjson/error/en.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fxl/logging.h"

// See wire_types.h for details.

namespace fidl_codec {

bool Type::ValueEquals(const uint8_t* /*bytes*/, size_t /*length*/,
                       const rapidjson::Value& /*value*/) const {
  FXL_LOG(FATAL) << "Equality operator for type not implemented";
  return false;
}

bool Type::ValueHas(const uint8_t* /*bytes*/, const rapidjson::Value& /*value*/) const {
  FXL_LOG(FATAL) << "ValueHas not implemented";
  return false;
}

size_t Type::InlineSize(MessageDecoder* decoder) const {
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

std::unique_ptr<Value> BoolType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  auto byte = decoder->GetAddress(offset, sizeof(uint8_t));
  return std::make_unique<BoolValue>(this, byte ? std::optional(*byte) : std::nullopt);
}

size_t StructType::InlineSize(MessageDecoder* decoder) const {
  return nullable_ ? sizeof(uintptr_t) : struct_.Size(decoder);
}

std::unique_ptr<Value> StructType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return struct_.DecodeObject(decoder, this, offset, nullable_);
}

size_t TableType::InlineSize(MessageDecoder* decoder) const { return table_.size(); }

std::unique_ptr<Value> TableType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t size = 0;
  decoder->GetValueAt(offset, &size);
  offset += sizeof(size);

  auto result = std::make_unique<TableValue>(this, table_, size);
  if (result->DecodeNullable(decoder, offset, size * 2 * sizeof(uint64_t))) {
    if (result->is_null()) {
      decoder->AddError() << std::hex << (decoder->absolute_offset() + offset) << std::dec
                          << ": Invalid null value for table pointer\n";
    }
  }
  return result;
}

UnionType::UnionType(const Union& uni, bool nullable) : union_(uni), nullable_(nullable) {}

size_t UnionType::InlineSize(MessageDecoder* decoder) const {
  FXL_DCHECK(decoder != nullptr);
  if (decoder->unions_are_xunions()) {
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

XUnionType::XUnionType(const XUnion& uni, bool nullable) : xunion_(uni), nullable_(nullable) {}

size_t XUnionType::InlineSize(MessageDecoder* decoder) const { return xunion_.size(); }

std::unique_ptr<Value> XUnionType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return xunion_.DecodeXUnion(decoder, this, offset, nullable_);
}

ElementSequenceType::ElementSequenceType(std::unique_ptr<Type>&& component_type)
    : component_type_(std::move(component_type)) {
  FXL_DCHECK(component_type_.get() != nullptr);
}

ArrayType::ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count)
    : ElementSequenceType(std::move(component_type)), count_(count) {}

std::unique_ptr<Value> ArrayType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  auto result = std::make_unique<ArrayValue>(this);
  for (uint64_t i = 0; i < count_; ++i) {
    result->AddValue(component_type_->Decode(decoder, offset));
    offset += component_type_->InlineSize(decoder);
  }
  return result;
}

VectorType::VectorType(std::unique_ptr<Type>&& component_type)
    : ElementSequenceType(std::move(component_type)) {}

std::unique_ptr<Value> VectorType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  uint64_t size = 0;
  decoder->GetValueAt(offset, &size);
  offset += sizeof(size);

  auto result = std::make_unique<VectorValue>(this, size, component_type_.get());

  // Don't need to check return value because the effects of returning false are
  // dealt with in DecodeNullable.
  result->DecodeNullable(decoder, offset, size * component_type_->InlineSize(decoder));
  return result;
}

EnumType::EnumType(const Enum& e) : enum_(e) {}

std::unique_ptr<Value> EnumType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return std::make_unique<EnumValue>(this, decoder->CopyAddress(offset, enum_.size()), enum_);
}

BitsType::BitsType(const Bits& b) : bits_(b) {}

std::unique_ptr<Value> BitsType::Decode(MessageDecoder* decoder, uint64_t offset) const {
  return std::make_unique<BitsValue>(this, decoder->CopyAddress(offset, bits_.size()), bits_);
}

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

}  // namespace fidl_codec
