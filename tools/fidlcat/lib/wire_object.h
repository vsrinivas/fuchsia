// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_WIRE_OBJECT_H_
#define TOOLS_FIDLCAT_LIB_WIRE_OBJECT_H_

#include <lib/fidl/cpp/message.h>
#include <src/lib/fxl/logging.h>

#include <memory>
#include <string_view>
#include <vector>

#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/message_decoder.h"

namespace fidlcat {

// Base class for all the fields we can find within a message.
class Field {
 public:
  explicit Field(std::string_view name) : name_(name) {}
  virtual ~Field() = default;

  const std::string& name() const { return name_; }

  // Decode the extra content of the field (in a secondary object).
  virtual void DecodeContent(MessageDecoder* decoder) = 0;

  // Extract the JSON for this field.
  virtual void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                           rapidjson::Value& result) const = 0;

 private:
  const std::string name_;
};

// Base class for fields which are nullable.
class NullableField : public Field {
 public:
  explicit NullableField(std::string_view name) : Field(name) {}

  bool is_null() const { return is_null_; }

  bool DecodeNullable(MessageDecoder* decoder, uint64_t offset);

 private:
  bool is_null_ = false;
};

// Base class for inlined fields (the data is not in a secondary object).
class InlineField : public Field {
 public:
  InlineField(std::string_view name, const uint8_t* data)
      : Field(name), data_(data) {}

  const uint8_t* data() const { return data_; }

  void DecodeContent(MessageDecoder* decoder) override;

 private:
  const uint8_t* const data_;
};

// A field with no known representation (we only print the raw data).
class RawField : public InlineField {
 public:
  RawField(std::string_view name, const uint8_t* data, uint64_t size)
      : InlineField(name, data), size_(size) {}

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const uint64_t size_;
};

// All numeric fields (integer and floating point numbers).
template <typename T>
class NumericField : public InlineField {
 public:
  NumericField(std::string_view name, const uint8_t* data)
      : InlineField(name, data) {}

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override {
    if (data() == nullptr) {
      result.SetString("(invalid)", allocator);
    } else {
      T val = internal::MemoryFrom<T, const uint8_t*>(data());
      result.SetString(std::to_string(val).c_str(), allocator);
    }
  }
};

// A string field.
class StringField : public NullableField {
 public:
  StringField(std::string_view name, uint64_t string_length)
      : NullableField(name), string_length_(string_length) {}

  void DecodeContent(MessageDecoder* decoder) override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const uint64_t string_length_;
  const uint8_t* data_ = nullptr;
};

// A Boolean field.
class BoolField : public InlineField {
 public:
  BoolField(std::string_view name, const uint8_t* data)
      : InlineField(name, data) {}

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override {
    if (data() == nullptr) {
      result.SetString("(invalid)", allocator);
    } else {
      uint8_t val = *data();
      result.SetString(val ? "true" : "false", allocator);
    }
  }
};

// An object. This represents a struct, a request or a response.
class Object : public NullableField {
 public:
  Object(std::string_view name, const Struct& struct_definition)
      : NullableField(name), struct_definition_(struct_definition) {}

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const Struct& struct_definition_;
  std::vector<std::unique_ptr<Field>> fields_;
};

// An envelope (used by TableField and XUnion).
class EnvelopeField : public NullableField {
 public:
  EnvelopeField(std::string_view name, const Type* type);

  uint32_t num_bytes() const { return num_bytes_; }
  uint32_t num_handles() const { return num_handles_; }
  const Field* field() const { return field_.get(); }
  void set_field(std::unique_ptr<Field> field) { field_ = std::move(field); }

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const Type* const type_;
  uint32_t num_bytes_ = 0;
  uint32_t num_handles_ = 0;
  std::unique_ptr<Field> field_ = nullptr;
};

// A table.
class TableField : public NullableField {
 public:
  TableField(std::string_view name, const Table& table_definition,
             uint64_t envelope_count);

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const Table& table_definition_;
  const uint64_t envelope_count_;
  std::vector<std::unique_ptr<EnvelopeField>> envelopes_;
};

// An union.
class UnionField : public NullableField {
 public:
  UnionField(std::string_view name, const Union& union_definition)
      : NullableField(name), union_definition_(union_definition) {}

  void set_field(std::unique_ptr<Field> field) { field_ = std::move(field); }

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const Union& union_definition_;
  std::unique_ptr<Field> field_;
};

// An xunion.
class XUnionField : public UnionField {
 public:
  XUnionField(std::string_view name, const XUnion& xunion_definition)
      : UnionField(name, xunion_definition) {}
};

// An array.
class ArrayField : public Field {
 public:
  explicit ArrayField(std::string_view name) : Field(name) {}

  void AddField(std::unique_ptr<Field> field) {
    fields_.push_back(std::move(field));
  }

  void DecodeContent(MessageDecoder* decoder) override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  std::vector<std::unique_ptr<Field>> fields_;
};

// A vector.
class VectorField : public NullableField {
 public:
  VectorField(std::string_view name, uint64_t size, const Type* component_type)
      : NullableField(name), size_(size), component_type_(component_type) {}

  void DecodeContent(MessageDecoder* decoder) override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const uint64_t size_;
  const Type* const component_type_;
  std::vector<std::unique_ptr<Field>> fields_;
};

// An enum.
class EnumField : public InlineField {
 public:
  EnumField(std::string_view name, const uint8_t* data,
            const Enum& enum_definition)
      : InlineField(name, data), enum_definition_(enum_definition) {}

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const Enum& enum_definition_;
};

// A handle.
class HandleField : public Field {
 public:
  HandleField(std::string_view name, zx_handle_t handle)
      : Field(name), handle_(handle) {}

  void DecodeContent(MessageDecoder* decoder) override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

 private:
  const zx_handle_t handle_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_WIRE_OBJECT_H_
