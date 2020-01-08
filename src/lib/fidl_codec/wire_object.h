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

  virtual bool IsNull() const { return false; }

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

  // Pretty print of the value.
  virtual void PrettyPrint(std::ostream& os, const Colors& colors,
                           const fidl_message_header_t* header, std::string_view line_header,
                           int tabs, int remaining_size, int max_line_size) const = 0;

  // Use a visitor on this value;
  virtual void Visit(Visitor* visitor) const = 0;

 private:
  const Type* const type_;
};

// An invalid value. This value can't be present in a valid object.
// It can only be found if we had an error while decoding a message.
class InvalidValue : public Value {
 public:
  explicit InvalidValue(const Type* type) : Value(type) {}

  int DisplaySize(int remaining_size) const override {
    constexpr int kInvalidSize = 7;
    return kInvalidSize;  // length of "invalid"
  }

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override {
    os << colors.red << "invalid" << colors.reset;
  }

  void Visit(Visitor* visitor) const override;
};

// A null value.
class NullValue : public Value {
 public:
  explicit NullValue(const Type* type) : Value(type) {}

  bool IsNull() const override { return true; }

  int DisplaySize(int remaining_size) const override {
    constexpr int kNullSize = 4;
    return kNullSize;  // length of "null"
  }

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override {
    os << colors.red << "null" << colors.reset;
  }

  void Visit(Visitor* visitor) const override;
};

// Base class for values which are nullable.
class NullableValue : public Value {
 public:
  NullableValue(const Type* type) : Value(type) {}

  bool IsNull() const override { return is_null_; }

  bool DecodeNullable(MessageDecoder* decoder, uint64_t offset, uint64_t size);

  // Decode the extra content of the value (in a secondary object).
  virtual void DecodeContent(MessageDecoder* decoder, uint64_t offset) = 0;

  void Visit(Visitor* visitor) const override;

 private:
  bool is_null_ = false;
};

// A value with no known representation (we only print the raw data).
class RawValue : public Value {
 public:
  RawValue(const Type* type, const uint8_t* data, size_t size)
      : Value(type), data_(data, data + size) {}

  const std::vector<uint8_t>& data() const { return data_; }

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const std::vector<uint8_t> data_;
};

// All numeric values (integer and floating point numbers).
template <typename T>
class NumericValue : public Value {
 public:
  explicit NumericValue(const Type* type, std::optional<T> value = std::nullopt)
      : Value(type), value_(std::move(value)) {}
  explicit NumericValue(const Type* type, const T* value)
      : NumericValue(type, value ? std::optional(*value) : std::nullopt) {}

  std::optional<T> value() const { return value_; }

  int DisplaySize(int remaining_size) const override {
    if (value_) {
      return std::to_string(*value_).size();
    } else {
      return 7;  // length of "invalid"
    }
  }

  uint8_t GetUint8Value() const override {
    return (sizeof(T) == 1 && value_) ? static_cast<uint8_t>(*value_) : 0;
  }

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override {
    if (value_) {
      os << colors.blue << std::to_string(*value_) << colors.reset;
    } else {
      os << colors.red << "invalid" << colors.reset;
    }
  }

  void Visit(Visitor* visitor) const override;

 private:
  std::optional<T> value_;
};

// A string value.
class StringValue : public Value {
 public:
  StringValue(const Type* type, std::string_view string) : Value(type), string_(string) {}

  const std::string& string() const { return string_; }

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const std::string string_;
};

// A Boolean value.
class BoolValue : public Value {
 public:
  BoolValue(const Type* type, uint8_t value) : Value(type), value_(value) {}

  uint8_t value() const { return value_; }

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const uint8_t value_;
};

// An instance of a Struct. This includes requests and responses which are also structs.
class StructValue : public NullableValue {
 public:
  StructValue(const Type* type, const Struct& struct_definition)
      : NullableValue(type), struct_definition_(struct_definition) {}

  const Struct& struct_definition() const { return struct_definition_; }
  const std::map<const StructMember*, std::unique_ptr<Value>>& fields() const { return fields_; }

  void AddField(const StructMember* member, std::unique_ptr<Value> value) {
    fields_.emplace(std::make_pair(member, std::move(value)));
  }

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
  std::map<const StructMember*, std::unique_ptr<Value>> fields_;
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

  const Table& table_definition() const { return table_definition_; }
  const std::map<const TableMember*, std::unique_ptr<Value>>& members() const { return members_; }
  Ordinal32 highest_member() const { return highest_member_; }

  void AddMember(const TableMember* member, std::unique_ptr<Value> value) {
    members_.emplace(std::make_pair(member, std::move(value)));
    if (member->ordinal() > highest_member_) {
      highest_member_ = member->ordinal();
    }
  }

  bool AddMember(std::string_view name, std::unique_ptr<Value> value);

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
  std::map<const TableMember*, std::unique_ptr<Value>> members_;
  Ordinal32 highest_member_ = 0;
};

// An union.
class UnionValue : public NullableValue {
 public:
  UnionValue(const Type* type, const Union& union_definition)
      : NullableValue(type), union_definition_(union_definition) {}

  const Union& union_definition() const { return union_definition_; }
  const UnionMember* member() const { return member_; }
  const std::unique_ptr<Value>& value() const { return value_; }

  void SetValue(const UnionMember* member, std::unique_ptr<Value> value) {
    member_ = member;
    value_ = std::move(value);
  }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder, uint64_t offset) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const Union& union_definition_;
  const UnionMember* member_ = nullptr;
  std::unique_ptr<Value> value_;
};

// An xunion.
class XUnionValue : public UnionValue {
 public:
  XUnionValue(const Type* type, const Union& union_definition)
      : UnionValue(type, union_definition) {}

  void Visit(Visitor* visitor) const override;
};

// An array.
class ArrayValue : public Value {
 public:
  ArrayValue(const Type* type) : Value(type) {}

  const std::vector<std::unique_ptr<Value>>& values() const { return values_; }

  void AddValue(std::unique_ptr<Value> value) { values_.push_back(std::move(value)); }

  int DisplaySize(int remaining_size) const override;

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
  VectorValue(const Type* type, uint64_t size) : NullableValue(type), size_(size) {}

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
  std::vector<std::unique_ptr<Value>> values_;
  bool is_string_ = false;
  bool has_new_line_ = false;
};

// An enum.
class EnumValue : public Value {
 public:
  EnumValue(const Type* type, std::optional<std::vector<uint8_t>> data, const Enum& enum_definition)
      : Value(type), enum_definition_(enum_definition), data_(std::move(data)) {}

  const std::optional<std::vector<uint8_t>>& data() const { return data_; }

  const Enum& enum_definition() const { return enum_definition_; };

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const Enum& enum_definition_;
  std::optional<std::vector<uint8_t>> data_;
};

// Bits.
class BitsValue : public Value {
 public:
  BitsValue(const Type* type, std::optional<std::vector<uint8_t>> data, const Bits& bits_definition)
      : Value(type), bits_definition_(bits_definition), data_(std::move(data)) {}

  const std::optional<std::vector<uint8_t>>& data() const { return data_; }

  const Bits& bits_definition() const { return bits_definition_; };

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const Bits& bits_definition_;
  std::optional<std::vector<uint8_t>> data_;
};

// A handle.
class HandleValue : public Value {
 public:
  HandleValue(const Type* type, const zx_handle_info_t& handle) : Value(type), handle_(handle) {}

  const zx_handle_info_t& handle() const { return handle_; }

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, const fidl_message_header_t* header,
                   std::string_view line_header, int tabs, int remaining_size,
                   int max_line_size) const override;

  void Visit(Visitor* visitor) const override;

 private:
  const zx_handle_info_t handle_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_
