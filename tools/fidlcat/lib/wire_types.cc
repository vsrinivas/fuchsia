// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_types.h"

#include <src/lib/fxl/logging.h>
#include <zircon/fidl.h>

#include "rapidjson/error/en.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_object.h"

// See wire_types.h for details.

namespace fidlcat {

bool Type::ValueEquals(const uint8_t* bytes, size_t length,
                       const rapidjson::Value& value) const {
  FXL_LOG(FATAL) << "Equality operator for type not implemented";
  return false;
}

size_t Type::InlineSize() const {
  FXL_LOG(FATAL) << "Size for type not implemented";
  return 0;
}

std::unique_ptr<Field> Type::Decode(MessageDecoder* decoder,
                                    std::string_view name,
                                    uint64_t offset) const {
  FXL_LOG(ERROR) << "Decode not implemented for field '" << name << "'";
  return nullptr;
}

std::unique_ptr<Field> RawType::Decode(MessageDecoder* decoder,
                                       std::string_view name,
                                       uint64_t offset) const {
  return std::make_unique<RawField>(
      name, decoder->GetAddress(offset, inline_size_), inline_size_);
}

std::unique_ptr<Field> StringType::Decode(MessageDecoder* decoder,
                                          std::string_view name,
                                          uint64_t offset) const {
  uint64_t string_length = 0;
  decoder->GetValueAt(offset, &string_length);
  offset += sizeof(string_length);

  auto result = std::make_unique<StringField>(name, string_length);
  result->DecodeNullable(decoder, offset);
  return result;
}

std::unique_ptr<Field> BoolType::Decode(MessageDecoder* decoder,
                                        std::string_view name,
                                        uint64_t offset) const {
  return std::make_unique<BoolField>(
      name, decoder->GetAddress(offset, sizeof(uint8_t)));
}

size_t StructType::InlineSize() const { return struct_.size(); }

std::unique_ptr<Field> StructType::Decode(MessageDecoder* decoder,
                                          std::string_view name,
                                          uint64_t offset) const {
  return struct_.DecodeObject(decoder, name, offset, nullable_);
}

size_t TableType::InlineSize() const { return table_.size(); }

std::unique_ptr<Field> TableType::Decode(MessageDecoder* decoder,
                                         std::string_view name,
                                         uint64_t offset) const {
  uint64_t size = 0;
  decoder->GetValueAt(offset, &size);
  offset += sizeof(size);

  auto result = std::make_unique<TableField>(name, table_, size);
  if (result->DecodeNullable(decoder, offset)) {
    if (result->is_null()) {
      FXL_LOG(ERROR) << "invalid null value for table pointer";
    }
  }
  return result;
}

UnionType::UnionType(const Union& uni, bool nullable)
    : union_(uni), nullable_(nullable) {}

size_t UnionType::InlineSize() const { return union_.size(); }

std::unique_ptr<Field> UnionType::Decode(MessageDecoder* decoder,
                                         std::string_view name,
                                         uint64_t offset) const {
  return union_.DecodeUnion(decoder, name, offset, nullable_);
}

XUnionType::XUnionType(const XUnion& uni, bool is_nullable)
    : xunion_(uni), is_nullable_(is_nullable) {}

size_t XUnionType::InlineSize() const { return xunion_.size(); }

std::unique_ptr<Field> XUnionType::Decode(MessageDecoder* decoder,
                                          std::string_view name,
                                          uint64_t offset) const {
  uint32_t ordinal = 0;
  if (decoder->GetValueAt(offset, &ordinal)) {
    if ((ordinal == 0) && !is_nullable_) {
      FXL_LOG(ERROR) << "null envelope for a non nullable extensible union";
    }
  }
  offset += sizeof(uint64_t);  // Skips ordinal + padding.

  std::unique_ptr<XUnionField> result =
      std::make_unique<XUnionField>(name, xunion_);

  std::unique_ptr<EnvelopeField> envelope;
  const UnionMember* member = xunion_.MemberWithOrdinal(ordinal);
  if (member == nullptr) {
    std::string key_name = std::string("unknown$") + std::to_string(ordinal);
    envelope = std::make_unique<EnvelopeField>(key_name, nullptr);
  } else {
    envelope = std::make_unique<EnvelopeField>(member->name(), member->type());
  }
  envelope->DecodeAt(decoder, offset);
  result->set_field(std::move(envelope));
  return result;
}

ElementSequenceType::ElementSequenceType(std::unique_ptr<Type>&& component_type)
    : component_type_(std::move(component_type)) {
  FXL_DCHECK(component_type_.get() != nullptr);
}

ArrayType::ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count)
    : ElementSequenceType(std::move(component_type)), count_(count) {}

std::unique_ptr<Field> ArrayType::Decode(MessageDecoder* decoder,
                                         std::string_view name,
                                         uint64_t offset) const {
  auto result = std::make_unique<ArrayField>(name);
  for (uint64_t i = 0; i < count_; ++i) {
    result->AddField(component_type_->Decode(decoder, "", offset));
    offset += component_type_->InlineSize();
  }
  return result;
}

VectorType::VectorType(std::unique_ptr<Type>&& component_type)
    : ElementSequenceType(std::move(component_type)) {}

std::unique_ptr<Field> VectorType::Decode(MessageDecoder* decoder,
                                          std::string_view name,
                                          uint64_t offset) const {
  uint64_t size = 0;
  decoder->GetValueAt(offset, &size);
  offset += sizeof(size);

  auto result =
      std::make_unique<VectorField>(name, size, component_type_.get());
  result->DecodeNullable(decoder, offset);
  return result;
}

EnumType::EnumType(const Enum& e) : enum_(e) {}

std::unique_ptr<Field> EnumType::Decode(MessageDecoder* decoder,
                                        std::string_view name,
                                        uint64_t offset) const {
  return std::make_unique<EnumField>(
      name, decoder->GetAddress(offset, enum_.size()), enum_);
}

std::unique_ptr<Field> HandleType::Decode(MessageDecoder* decoder,
                                          std::string_view name,
                                          uint64_t offset) const {
  zx_handle_t handle = FIDL_HANDLE_ABSENT;
  decoder->GetValueAt(offset, &handle);
  if ((handle != FIDL_HANDLE_ABSENT) && (handle != FIDL_HANDLE_PRESENT)) {
    FXL_LOG(ERROR) << "invalid value <" << std::hex << handle << std::dec
                   << "> for handle";
    return std::make_unique<HandleField>(name, FIDL_HANDLE_ABSENT);
  }
  if (handle != FIDL_HANDLE_ABSENT) {
    handle = decoder->GetNextHandle();
  }
  return std::make_unique<HandleField>(name, handle);
}

std::unique_ptr<Type> Type::ScalarTypeFromName(const std::string& type_name,
                                               size_t inline_size) {
  static std::map<std::string, std::function<std::unique_ptr<Type>()>>
      scalar_type_map_{
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

std::unique_ptr<Type> Type::TypeFromPrimitive(const rapidjson::Value& type,
                                              size_t inline_size) {
  if (!type.HasMember("subtype")) {
    FXL_LOG(ERROR) << "Invalid type";
    return std::make_unique<RawType>(inline_size);
  }

  std::string subtype = type["subtype"].GetString();
  return ScalarTypeFromName(subtype, inline_size);
}

std::unique_ptr<Type> Type::TypeFromIdentifier(LibraryLoader* loader,
                                               const rapidjson::Value& type,
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

std::unique_ptr<Type> Type::GetType(LibraryLoader* loader,
                                    const rapidjson::Value& type,
                                    size_t inline_size) {
  // TODO: This is creating a new type every time we need one.  That's pretty
  // inefficient.  Find a way of caching them if it becomes a problem.
  if (!type.HasMember("kind")) {
    FXL_LOG(ERROR) << "Invalid type";
    return std::make_unique<RawType>(inline_size);
  }
  std::string kind = type["kind"].GetString();
  if (kind == "array") {
    const rapidjson::Value& element_type = type["element_type"];
    uint32_t element_count =
        std::strtol(type["element_count"].GetString(), nullptr, 10);
    return std::make_unique<ArrayType>(GetType(loader, element_type, 0),
                                       element_count);
  } else if (kind == "vector") {
    const rapidjson::Value& element_type = type["element_type"];
    return std::make_unique<VectorType>(GetType(loader, element_type, 0));
  } else if (kind == "string") {
    return std::make_unique<StringType>();
  } else if (kind == "handle") {
    return std::make_unique<HandleType>();
  } else if (kind == "request") {
    return std::make_unique<HandleType>();
  } else if (kind == "primitive") {
    return Type::TypeFromPrimitive(type, inline_size);
  } else if (kind == "identifier") {
    return Type::TypeFromIdentifier(loader, type, inline_size);
  }
  FXL_LOG(ERROR) << "Invalid type " << kind;
  return std::make_unique<RawType>(inline_size);
}

}  // namespace fidlcat
