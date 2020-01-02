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
  Value() = default;
  virtual ~Value() = default;

  virtual bool IsNull() const { return false; }

  // Returns the uint8_t value of the value. If the value is not a uint8_t value this returns zero.
  // This is used to eventually display a vector of uint8_t values as a string.
  virtual uint8_t GetUint8Value() const { return 0; }

  // Gets the integer value of the value. Returns false if the node can't compute an integer value.
  // For floating point values, the floating point value is converted to the nearest integer
  // value.
  virtual bool GetIntegerValue(uint64_t* absolute, bool* negative) const { return false; }

  // Gets the floating point value of the value. Returns false if the node can't compute a floating
  // point value. For integer values, we can lost precision during the conversion.
  virtual bool GetDoubleValue(double* result) const { return false; }

  // Returns the size needed to display the value. If the needed size is
  // greater than |remaining_size|, the return value can be anything greater
  // than |remaining_size| and the only useful information is that the value
  // can't fit in |remaining_size|.
  // Remaining size is just an optimization parameter. It avoids to compute the
  // whole display size for an object: the computation is stopped as soon as we
  // find that the object doesn't fit.
  virtual int DisplaySize(const Type* for_type, int remaining_size) const = 0;

  // Pretty print of the value.
  virtual void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                           const fidl_message_header_t* header, std::string_view line_header,
                           int tabs, int remaining_size, int max_line_size) const = 0;

  // Use a visitor on this value;
  virtual void Visit(Visitor* visitor, const Type* for_type) const = 0;
};

// An invalid value. This value can't be present in a valid object.
// It can only be found if we had an error while decoding a message.
class InvalidValue : public Value {
 public:
  InvalidValue() = default;

  int DisplaySize(const Type* for_type, int remaining_size) const override {
    constexpr int kInvalidSize = 7;
    return kInvalidSize;  // length of "invalid"
  }

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override {
    os << colors.red << "invalid" << colors.reset;
  }

  void Visit(Visitor* visitor, const Type* for_type) const override;
};

// A null value.
class NullValue : public Value {
 public:
  NullValue() = default;

  bool IsNull() const override { return true; }

  int DisplaySize(const Type* for_type, int remaining_size) const override {
    constexpr int kNullSize = 4;
    return kNullSize;  // length of "null"
  }

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override {
    os << colors.red << "null" << colors.reset;
  }

  void Visit(Visitor* visitor, const Type* for_type) const override;
};

// A value with no known representation (we only print the raw data).
class RawValue : public Value {
 public:
  RawValue(const uint8_t* data, size_t size) : data_(data, data + size) {}

  const std::vector<uint8_t>& data() const { return data_; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const std::vector<uint8_t> data_;
};

// A Boolean value.
class BoolValue : public Value {
 public:
  explicit BoolValue(uint8_t value) : value_(value) {}

  uint8_t value() const { return value_; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const uint8_t value_;
};

class IntegerValue : public Value {
 public:
  IntegerValue(uint64_t absolute_value, bool negative)
      : absolute_value_(absolute_value), negative_(negative) {}

  uint64_t absolute_value() const { return absolute_value_; }
  bool negative() const { return negative_; }

  uint8_t GetUint8Value() const override;

  bool GetIntegerValue(uint64_t* absolute, bool* negative) const override {
    *absolute = absolute_value_;
    *negative = negative_;
    return true;
  }

  bool GetDoubleValue(double* result) const override {
    *result = absolute_value_;
    if (negative_) {
      *result = -(*result);
    }
    return true;
  }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const uint64_t absolute_value_;
  const bool negative_;
};

class DoubleValue : public Value {
 public:
  explicit DoubleValue(double value) : value_(value) {}

  double value() const { return value_; }

  bool GetIntegerValue(uint64_t* absolute, bool* negative) const override {
    if (value_ < 0) {
      *absolute = static_cast<uint64_t>(-value_);
      *negative = true;
    } else {
      *absolute = static_cast<uint64_t>(value_);
      *negative = false;
    }
    return true;
  }

  bool GetDoubleValue(double* result) const override {
    *result = value_;
    return true;
  }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const double value_;
};

// A string value.
class StringValue : public Value {
 public:
  explicit StringValue(std::string_view string) : string_(string) {}

  const std::string& string() const { return string_; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const std::string string_;
};

// A handle.
class HandleValue : public Value {
 public:
  explicit HandleValue(const zx_handle_info_t& handle) : handle_(handle) {}

  const zx_handle_info_t& handle() const { return handle_; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const zx_handle_info_t handle_;
};

// An union.
class UnionValue : public Value {
 public:
  UnionValue(const UnionMember& member, std::unique_ptr<Value> value)
      : member_(member), value_(std::move(value)) {}

  const UnionMember& member() const { return member_; }
  const std::unique_ptr<Value>& value() const { return value_; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const UnionMember& member_;
  const std::unique_ptr<Value> value_;
};

// An instance of a Struct. This includes requests and responses which are also structs.
class StructValue : public Value {
 public:
  explicit StructValue(const Struct& struct_definition) : struct_definition_(struct_definition) {}

  const Struct& struct_definition() const { return struct_definition_; }
  const std::map<const StructMember*, std::unique_ptr<Value>>& fields() const { return fields_; }

  void AddField(const StructMember* member, std::unique_ptr<Value> value) {
    fields_.emplace(std::make_pair(member, std::move(value)));
  }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

  // Extract the JSON for this object.
  void ExtractJson(rapidjson::Document::AllocatorType& allocator, rapidjson::Value& result) const;

 private:
  const Struct& struct_definition_;
  std::map<const StructMember*, std::unique_ptr<Value>> fields_;
};

// A vector.
class VectorValue : public Value {
 public:
  VectorValue() = default;

  const std::vector<std::unique_ptr<Value>>& values() const { return values_; }

  void AddValue(std::unique_ptr<Value> value) {
    if (value == nullptr) {
      is_string_ = false;
    } else {
      uint8_t uvalue = value->GetUint8Value();
      if (!std::isprint(uvalue)) {
        if ((uvalue == '\r') || (uvalue == '\n')) {
          has_new_line_ = true;
        } else {
          is_string_ = false;
        }
      }
    }
    values_.push_back(std::move(value));
  }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  std::vector<std::unique_ptr<Value>> values_;
  bool is_string_ = true;
  bool has_new_line_ = false;
};

// A table.
class TableValue : public Value {
 public:
  explicit TableValue(const Table& table_definition) : table_definition_(table_definition) {}

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

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const Table& table_definition_;
  std::map<const TableMember*, std::unique_ptr<Value>> members_;
  Ordinal32 highest_member_ = 0;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_
