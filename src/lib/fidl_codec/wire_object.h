// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_
#define SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_

#include <lib/fidl/cpp/message.h>

#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fxl/logging.h"

namespace fidl_codec {

class Visitor;

// Base class for all the values we can find within a message.
class Value {
 public:
  Value(const Type* type) : type_(type) {}
  virtual ~Value() = default;

  const Type* type() const { return type_; }

  virtual bool is_null() const { return false; }

  // Returns the size needed to display the value. If the needed size is
  // greater than |remaining_size|, the return value can be anything greater
  // than |remaining_size| and the only useful information is that the value
  // can't fit in |remaining_size|.
  // Remaining size is just an optimization parameter. It avoids to compute the
  // whole display size for an object: the computation is stopped as soon as we
  // find that the object doesn't fit.
  virtual int DisplaySize(int remaining_size) const = 0;

  // Returns the uint8_t value of the value. If the value is not a uint8_t value this returns zero.
  // This is used to eventually display a vector of uint8_t values as a string.
  virtual uint8_t GetUint8Value() const { return 0; }

  // Decode the extra content of the value (in a secondary object).
  virtual void DecodeContent(MessageDecoder* decoder, uint64_t offset) = 0;

  // Pretty print of the value.
  virtual void PrettyPrint(std::ostream& os, const Colors& colors,
                           const fidl_message_header_t* header, std::string_view line_header,
                           int tabs, int remaining_size, int max_line_size) const = 0;

  // Use a visitor on this value;
  virtual void Visit(Visitor* visitor) const = 0;

 private:
  const Type* const type_;
};

// A name-value pair.
class Field {
 public:
  Field() = default;
  Field(const std::string& name, std::unique_ptr<Value> value)
      : name_(name), value_(std::move(value)) {}

  const std::string& name() const { return name_; }
  const std::unique_ptr<Value>& value() const { return value_; }

 private:
  std::string name_;
  std::unique_ptr<Value> value_;
};

// Base class for values which are nullable.
class NullableValue : public Value {
 public:
  NullableValue(const Type* type) : Value(type) {}

  bool is_null() const override { return is_null_; }

  bool DecodeNullable(MessageDecoder* decoder, uint64_t offset, uint64_t size);

  void Visit(Visitor* visitor) const override;

 private:
  bool is_null_ = false;
};

// A name-value pair where the value is nullable.
class NullableField {
 public:
  NullableField() = default;
  NullableField(const std::string& name, std::unique_ptr<NullableValue> value)
      : name_(name), value_(std::move(value)) {}

  const std::string& name() const { return name_; }
  const std::unique_ptr<NullableValue>& value() const { return value_; }

 private:
  std::string name_;
  std::unique_ptr<NullableValue> value_;
};

// Base class for inlined values (the data is not in a secondary object).
class InlineValue : public Value {
 public:
  InlineValue(const Type* type, const uint8_t* data) : Value(type), data_(data) {}

  const uint8_t* data() const { return data_; }

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void Visit(Visitor* visitor) const override;

 private:
  const uint8_t* const data_;
};

// A value with no known representation (we only print the raw data).
class RawValue : public InlineValue {
 public:
  RawValue(const Type* type, const uint8_t* data, uint64_t size)
      : InlineValue(type, data), size_(size) {}

  uint64_t size() const { return size_; }

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const uint64_t size_;
};

// All numeric values (integer and floating point numbers).
template <typename T>
class NumericValue : public InlineValue {
 public:
  NumericValue(const Type* type, const uint8_t* data) : InlineValue(type, data) {}

  int DisplaySize(int remaining_size) const override {
    return (data() == nullptr)
               ? 7
               : std::to_string(internal::MemoryFrom<T, const uint8_t*>(data())).size();
  }

  uint8_t GetUint8Value() const override {
    return ((data() != nullptr) && (sizeof(T) == 1))
               ? internal::MemoryFrom<T, const uint8_t*>(data())
               : 0;
  }

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override {
    if (data() == nullptr) {
      os << colors.red << "invalid" << colors.reset;
    } else {
      os << colors.blue << std::to_string(internal::MemoryFrom<T, const uint8_t*>(data()))
         << colors.reset;
    }
  }

  void Visit(Visitor* visitor) const override;
};

// A string value.
class StringValue : public NullableValue {
 public:
  StringValue(const Type* type, uint64_t string_length)
      : NullableValue(type), string_length_(string_length) {}

  uint64_t string_length() const { return string_length_; }
  const uint8_t* data() const { return data_; }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const uint64_t string_length_;
  const uint8_t* data_ = nullptr;
};

// A Boolean value.
class BoolValue : public InlineValue {
 public:
  BoolValue(const Type* type, const uint8_t* data) : InlineValue(type, data) {}

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;
};

// An object. This represents a struct, a request or a response.
class Object : public NullableValue {
 public:
  Object(const Type* type, const Struct& struct_definition)
      : NullableValue(type), struct_definition_(struct_definition) {}

  const std::map<std::string, std::unique_ptr<Value>>& fields() const { return fields_; }
  const Struct& struct_definition() const { return struct_definition_; }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

  // Extract the JSON for this object.
  void ExtractJson(rapidjson::Document::AllocatorType& allocator, rapidjson::Value& result) const;

 private:
  const Struct& struct_definition_;
  std::map<std::string, std::unique_ptr<Value>> fields_;
};

// An envelope (used by TableValue and XUnion).
class EnvelopeValue : public NullableValue {
 public:
  EnvelopeValue(const Type* type);

  uint32_t num_bytes() const { return num_bytes_; }
  uint32_t num_handles() const { return num_handles_; }
  const Value* value() const { return value_.get(); }
  void set_value(std::unique_ptr<Value> value) { value_ = std::move(value); }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  uint32_t num_bytes_ = 0;
  uint32_t num_handles_ = 0;
  std::unique_ptr<Value> value_ = nullptr;
};

// A table.
class TableValue : public NullableValue {
 public:
  TableValue(const Type* type, const Table& table_definition, uint64_t envelope_count);

  const std::vector<NullableField>& envelopes() const { return envelopes_; }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const Table& table_definition_;
  const uint64_t envelope_count_;
  std::vector<NullableField> envelopes_;
};

// An union.
class UnionValue : public NullableValue {
 public:
  UnionValue(const Type* type, const Union& union_definition)
      : NullableValue(type), union_definition_(union_definition) {}

  const Union& definition() const { return union_definition_; }

  const Field& field() const { return field_; }
  void set_field(Field field) { field_ = std::move(field); }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const Union& union_definition_;
  Field field_;
};

// An xunion.
class XUnionValue : public UnionValue {
 public:
  XUnionValue(const Type* type, const Union& xunion_definition)
      : UnionValue(type, xunion_definition) {}

  void Visit(Visitor* visitor) const override;
};

// An array.
class ArrayValue : public Value {
 public:
  ArrayValue(const Type* type) : Value(type) {}

  const std::vector<std::unique_ptr<Value>>& values() const { return values_; }

  void AddValue(std::unique_ptr<Value> value) { values_.push_back(std::move(value)); }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  std::vector<std::unique_ptr<Value>> values_;
};

// A vector.
class VectorValue : public NullableValue {
 public:
  VectorValue(const Type* type, uint64_t size, const Type* component_type)
      : NullableValue(type), size_(size), component_type_(component_type) {}

  size_t size() const { return size_; }
  const std::vector<std::unique_ptr<Value>>& values() const { return values_; }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const uint64_t size_;
  const Type* const component_type_;
  std::vector<std::unique_ptr<Value>> values_;
  bool is_string_ = false;
  bool has_new_line_ = false;
};

// An enum.
class EnumValue : public InlineValue {
 public:
  EnumValue(const Type* type, const uint8_t* data, const Enum& enum_definition)
      : InlineValue(type, data), enum_definition_(enum_definition) {}

  const Enum& enum_definition() const { return enum_definition_; };

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const Enum& enum_definition_;
};

// Bits.
class BitsValue : public InlineValue {
 public:
  BitsValue(const Type* type, const uint8_t* data, const Bits& bits_definition)
      : InlineValue(type, data), bits_definition_(bits_definition) {}

  const Bits& bits_definition() const { return bits_definition_; };

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const Bits& bits_definition_;
};

// A handle.
class HandleValue : public Value {
 public:
  HandleValue(const Type* type, const zx_handle_info_t& handle) : Value(type), handle_(handle) {}

  const zx_handle_info_t& handle() const { return handle_; }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const zx_handle_info_t handle_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_
