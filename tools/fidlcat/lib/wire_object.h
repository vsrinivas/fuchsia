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

constexpr int kTabSize = 2;

struct Colors {
  Colors(const char* new_reset, const char* new_red, const char* new_green,
         const char* new_blue, const char* new_white_on_magenta)
      : reset(new_reset),
        red(new_red),
        green(new_green),
        blue(new_blue),
        white_on_magenta(new_white_on_magenta) {}

  const char* const reset;
  const char* const red;
  const char* const green;
  const char* const blue;
  const char* const white_on_magenta;
};

extern const Colors WithoutColors;
extern const Colors WithColors;

// Base class for all the fields we can find within a message.
class Field {
 public:
  Field(std::string_view name, const Type* type) : name_(name), type_(type) {}
  virtual ~Field() = default;

  const std::string& name() const { return name_; }
  const Type* type() const { return type_; }

  // Returns the size needed to display the field. If the needed size is
  // greater than |remaining_size|, the return value can be anything greater
  // than |remaining_size| and the only useful information is that the field
  // can't fit in |remaining_size|.
  // Remaining size is just an optimization parameter. It avoids to compute the
  // whole display size for an object: the computation is stopped as soon as we
  // find that the object doesn't fit.
  virtual int DisplaySize(int remaining_size) const = 0;

  // Decode the extra content of the field (in a secondary object).
  virtual void DecodeContent(MessageDecoder* decoder) = 0;

  // Extract the JSON for this field.
  virtual void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                           rapidjson::Value& result) const;

  // Pretty print of the field.
  virtual void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                           int remaining_size, int max_line_size) const = 0;

 private:
  const std::string name_;
  const Type* const type_;
};

// Base class for fields which are nullable.
class NullableField : public Field {
 public:
  NullableField(std::string_view name, const Type* type) : Field(name, type) {}

  bool is_null() const { return is_null_; }

  bool DecodeNullable(MessageDecoder* decoder, uint64_t offset);

 private:
  bool is_null_ = false;
};

// Base class for inlined fields (the data is not in a secondary object).
class InlineField : public Field {
 public:
  InlineField(std::string_view name, const Type* type, const uint8_t* data)
      : Field(name, type), data_(data) {}

  const uint8_t* data() const { return data_; }

  void DecodeContent(MessageDecoder* decoder) override;

 private:
  const uint8_t* const data_;
};

// A field with no known representation (we only print the raw data).
class RawField : public InlineField {
 public:
  RawField(std::string_view name, const Type* type, const uint8_t* data,
           uint64_t size)
      : InlineField(name, type, data), size_(size) {}

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  const uint64_t size_;
};

// All numeric fields (integer and floating point numbers).
template <typename T>
class NumericField : public InlineField {
 public:
  NumericField(std::string_view name, const Type* type, const uint8_t* data)
      : InlineField(name, type, data) {}

  int DisplaySize(int remaining_size) const override {
    return (data() == nullptr)
               ? 7
               : std::to_string(internal::MemoryFrom<T, const uint8_t*>(data()))
                     .size();
  }

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override {
    if (data() == nullptr) {
      os << colors.red << "invalid" << colors.reset;
    } else {
      os << colors.blue
         << std::to_string(internal::MemoryFrom<T, const uint8_t*>(data()))
         << colors.reset;
    }
  }
};

// A string field.
class StringField : public NullableField {
 public:
  StringField(std::string_view name, const Type* type, uint64_t string_length)
      : NullableField(name, type), string_length_(string_length) {}

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  const uint64_t string_length_;
  const uint8_t* data_ = nullptr;
};

// A Boolean field.
class BoolField : public InlineField {
 public:
  BoolField(std::string_view name, const Type* type, const uint8_t* data)
      : InlineField(name, type, data) {}

  int DisplaySize(int remaining_size) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;
};

// An object. This represents a struct, a request or a response.
class Object : public NullableField {
 public:
  Object(std::string_view name, const Type* type,
         const Struct& struct_definition)
      : NullableField(name, type), struct_definition_(struct_definition) {}

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

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

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  uint32_t num_bytes_ = 0;
  uint32_t num_handles_ = 0;
  std::unique_ptr<Field> field_ = nullptr;
};

// A table.
class TableField : public NullableField {
 public:
  TableField(std::string_view name, const Type* type,
             const Table& table_definition, uint64_t envelope_count);

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  const Table& table_definition_;
  const uint64_t envelope_count_;
  std::vector<std::unique_ptr<EnvelopeField>> envelopes_;
};

// An union.
class UnionField : public NullableField {
 public:
  UnionField(std::string_view name, const Type* type,
             const Union& union_definition)
      : NullableField(name, type), union_definition_(union_definition) {}

  void set_field(std::unique_ptr<Field> field) { field_ = std::move(field); }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void DecodeAt(MessageDecoder* decoder, uint64_t base_offset);

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  const Union& union_definition_;
  std::unique_ptr<Field> field_;
};

// An xunion.
class XUnionField : public UnionField {
 public:
  XUnionField(std::string_view name, const Type* type,
              const XUnion& xunion_definition)
      : UnionField(name, type, xunion_definition) {}
};

// An array.
class ArrayField : public Field {
 public:
  ArrayField(std::string_view name, const Type* type) : Field(name, type) {}

  void AddField(std::unique_ptr<Field> field) {
    fields_.push_back(std::move(field));
  }

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  std::vector<std::unique_ptr<Field>> fields_;
};

// A vector.
class VectorField : public NullableField {
 public:
  VectorField(std::string_view name, const Type* type, uint64_t size,
              const Type* component_type)
      : NullableField(name, type),
        size_(size),
        component_type_(component_type) {}

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  const uint64_t size_;
  const Type* const component_type_;
  std::vector<std::unique_ptr<Field>> fields_;
};

// An enum.
class EnumField : public InlineField {
 public:
  EnumField(std::string_view name, const Type* type, const uint8_t* data,
            const Enum& enum_definition)
      : InlineField(name, type, data), enum_definition_(enum_definition) {}

  int DisplaySize(int remaining_size) const override;

  void ExtractJson(rapidjson::Document::AllocatorType& allocator,
                   rapidjson::Value& result) const override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  const Enum& enum_definition_;
};

// A handle.
class HandleField : public Field {
 public:
  HandleField(std::string_view name, const Type* type, zx_handle_t handle)
      : Field(name, type), handle_(handle) {}

  int DisplaySize(int remaining_size) const override;

  void DecodeContent(MessageDecoder* decoder) override;

  void PrettyPrint(std::ostream& os, const Colors& colors, int tabs,
                   int remaining_size, int max_line_size) const override;

 private:
  const zx_handle_t handle_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_WIRE_OBJECT_H_
