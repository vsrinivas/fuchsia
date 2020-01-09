// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_WIRE_TYPES_H_
#define SRC_LIB_FIDL_CODEC_WIRE_TYPES_H_

#include <lib/fit/function.h>

#ifdef __Fuchsia__
#include <zircon/types.h>
#else
typedef uint32_t zx_handle_t;
#endif

#include <sstream>
#include <string>
#include <vector>

#include "rapidjson/document.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/memory_helpers.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {

class TypeVisitor;

// A FIDL type.  Provides methods for generating instances of this type.
class Type {
  friend class InterfaceMethodParameter;
  friend class Library;

 public:
  Type() = default;
  virtual ~Type() = default;

  // Returns true if the type is a XUnionType.
  virtual bool IsXUnion() const { return false; }

  // Returns true if the type is a ArrayType.
  virtual bool IsArray() const { return false; }

  // Returns a detailed representation of the type.
  std::string ToString(bool expand = false) const;

  // Returns a readable representation of the type.
  virtual std::string Name() const = 0;

  // Returns the size of this type when embedded in another object.
  virtual size_t InlineSize(bool unions_are_xunions) const;

  // Whether this is a nullable type.
  virtual bool Nullable() const { return false; }

  // For vectors and arrays, give the type of the components (members).
  virtual const Type* GetComponentType() const { return nullptr; }

  // Decodes the type's inline part. It generates a Value and, eventually,
  // registers the field for further decoding (secondary objects).
  virtual std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const;

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type (e.g., "array", "vector", "foo.bar/Baz").
  // |loader| is the set of libraries to use to find types that need to be given
  // by identifier (e.g., "foo.bar/Baz").
  static std::unique_ptr<Type> GetType(LibraryLoader* loader, const rapidjson::Value& type,
                                       size_t inline_size);

  // Gets a Type object representing the |type|.  |type| is a JSON object with a
  // "subtype" field that represents a scalar type (e.g., "float64", "uint32")
  static std::unique_ptr<Type> TypeFromPrimitive(const rapidjson::Value& type, size_t inline_size);

  // Gets a Type object representing the |type_name|.  |type| is a string that
  // represents a scalar type (e.g., "float64", "uint32").
  static std::unique_ptr<Type> ScalarTypeFromName(const std::string& type_name, size_t inline_size);

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type.  "kind" is an identifier
  // (e.g.,"foo.bar/Baz").  |loader| is the set of libraries to use to lookup
  // that identifier.
  static std::unique_ptr<Type> TypeFromIdentifier(LibraryLoader* loader,
                                                  const rapidjson::Value& type, size_t inline_size);

  // Pretty prints the value for this type. This is used to print numerical values.
  virtual void PrettyPrint(const Value* value, std::ostream& os, const Colors& colors,
                           const fidl_message_header_t* header, std::string_view line_header,
                           int tabs, int remaining_size, int max_line_size) const {
    os << colors.red << "invalid" << colors.reset;
  }

  // Use a visitor on this value;
  virtual void Visit(TypeVisitor* visitor) const = 0;

  Type& operator=(const Type& other) = default;
  Type(const Type& other) = default;
};

// An instance of this class is created when the system can't determine the real
// class (e.g., in cases of corrupted metadata). Only a hexa dump is generated.
class RawType : public Type {
 public:
  explicit RawType(size_t inline_size) : inline_size_(inline_size) {}

  std::string Name() const override { return "unknown"; }

  size_t InlineSize(bool unions_are_xunions) const override { return inline_size_; }

  bool Nullable() const override { return true; }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  size_t inline_size_;
};

class StringType : public Type {
 public:
  std::string Name() const override { return "string"; }

  size_t InlineSize(bool unions_are_xunions) const override {
    return sizeof(uint64_t) + sizeof(uint64_t);
  }

  bool Nullable() const override { return true; }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

// A generic type that can be used for any numeric value that corresponds to a
// C++ arithmetic value.
template <typename T>
class NumericType : public Type {
  static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value,
                "NumericType can only be used for numerics");

 public:
  size_t InlineSize(bool unions_are_xunions) const override { return sizeof(T); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override {
    auto got = decoder->GetAddress(offset, sizeof(T));
    if (got == nullptr) {
      return std::make_unique<InvalidValue>();
    }
    return std::make_unique<DoubleValue>(*reinterpret_cast<const T*>(got));
  }

  void PrettyPrint(const Value* value, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override {
    double result;
    if (!value->GetDoubleValue(&result)) {
      os << colors.red << "invalid" << colors.reset;
    } else {
      os << colors.blue << std::to_string(result) << colors.reset;
    }
  }
};

// A generic type that can be used for any integer numeric value that corresponds to a
// C++ integral value.
template <typename T>
class IntegralType : public NumericType<T> {
  static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value,
                "IntegralType can only be used for integers");

 public:
  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override {
    auto got = decoder->GetAddress(offset, sizeof(T));
    if (got == nullptr) {
      return std::make_unique<InvalidValue>();
    }
    T value = *reinterpret_cast<const T*>(got);
    if (value < 0) {
      int64_t tmp = value;
      return std::make_unique<IntegerValue>(-tmp, true);
    }
    return std::make_unique<IntegerValue>(value, false);
  }

  void PrettyPrint(const Value* value, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override {
    uint64_t absolute;
    bool negative;
    if (!value->GetIntegerValue(&absolute, &negative)) {
      os << colors.red << "invalid" << colors.reset;
    } else {
      os << colors.blue;
      if (negative) {
        os << '-';
      }
      os << std::to_string(absolute) << colors.reset;
    }
  }
};

class Float32Type : public NumericType<float> {
 public:
  std::string Name() const override { return "float32"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Float64Type : public NumericType<double> {
 public:
  std::string Name() const override { return "float64"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Int8Type : public IntegralType<int8_t> {
 public:
  std::string Name() const override { return "int8"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Int16Type : public IntegralType<int16_t> {
 public:
  std::string Name() const override { return "int16"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Int32Type : public IntegralType<int32_t> {
 public:
  std::string Name() const override { return "int32"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Int64Type : public IntegralType<int64_t> {
 public:
  std::string Name() const override { return "int64"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Uint8Type : public IntegralType<uint8_t> {
 public:
  std::string Name() const override { return "uint8"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Uint16Type : public IntegralType<uint16_t> {
 public:
  std::string Name() const override { return "uint16"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Uint32Type : public IntegralType<uint32_t> {
 public:
  std::string Name() const override { return "uint32"; }
  void Visit(TypeVisitor* visitor) const override;
};

class Uint64Type : public IntegralType<uint64_t> {
 public:
  std::string Name() const override { return "uint64"; }
  void Visit(TypeVisitor* visitor) const override;
};

class BoolType : public Type {
 public:
  std::string Name() const override { return "bool"; }

  size_t InlineSize(bool unions_are_xunions) const override { return sizeof(uint8_t); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

class StructType : public Type {
 public:
  StructType(const Struct& str, bool nullable) : struct_(str), nullable_(nullable) {}

  const Struct& struct_definition() const { return struct_; }

  std::string Name() const override { return struct_.name(); }

  size_t InlineSize(bool unions_are_xunions) const override;

  bool Nullable() const override { return nullable_; }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Struct& struct_;
  const bool nullable_;
};

class TableType : public Type {
 public:
  explicit TableType(const Table& table_definition) : table_definition_(table_definition) {}

  const Table& table_definition() const { return table_definition_; }

  std::string Name() const override { return table_definition_.name(); }

  size_t InlineSize(bool unions_are_xunions) const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Table& table_definition_;
};

class UnionType : public Type {
 public:
  UnionType(const Union& uni, bool nullable);

  const Union& union_definition() const { return union_; }

  std::string Name() const override { return union_.name(); }

  size_t InlineSize(bool unions_are_xunions) const override;

  bool Nullable() const override { return nullable_; }

  std::unique_ptr<Value> DecodeUnion(MessageDecoder* decoder, uint64_t offset) const;
  std::unique_ptr<Value> DecodeXUnion(MessageDecoder* decoder, uint64_t offset) const;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Union& union_;
  const bool nullable_;
};

class XUnionType : public UnionType {
 public:
  XUnionType(const XUnion& uni, bool nullable) : UnionType(uni, nullable) {}

  bool IsXUnion() const override { return true; }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

class ElementSequenceType : public Type {
 public:
  explicit ElementSequenceType(std::unique_ptr<Type>&& component_type);

  const Type* component_type() const { return component_type_.get(); }

  const Type* GetComponentType() const override { return component_type_.get(); }

  void Visit(TypeVisitor* visitor) const override;

 protected:
  std::unique_ptr<Type> component_type_;
};

class ArrayType : public ElementSequenceType {
 public:
  ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count);

  bool IsArray() const override { return true; }

  std::string Name() const override {
    return std::string("array<") + component_type_->Name() + ">";
  }

  size_t InlineSize(bool unions_are_xunions) const override {
    return component_type_->InlineSize(unions_are_xunions) * count_;
  }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  uint32_t count_;
};

class VectorType : public ElementSequenceType {
 public:
  explicit VectorType(std::unique_ptr<Type>&& component_type);

  std::string Name() const override {
    return std::string("vector<") + component_type_->Name() + ">";
  }

  size_t InlineSize(bool unions_are_xunions) const override {
    return sizeof(uint64_t) + sizeof(uint64_t);
  }

  bool Nullable() const override { return true; }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

class EnumType : public Type {
 public:
  explicit EnumType(const Enum& e);

  const Enum& enum_definition() const { return enum_; }

  std::string Name() const override { return enum_.name(); }

  size_t InlineSize(bool unions_are_xunions) const override { return enum_.size(); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void PrettyPrint(const Value* value, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Enum& enum_;
};

class BitsType : public Type {
 public:
  explicit BitsType(const Bits& b);

  const Bits& bits_definition() const { return bits_; }

  std::string Name() const override { return bits_.name(); }

  size_t InlineSize(bool unions_are_xunions) const override { return bits_.size(); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void PrettyPrint(const Value* value, std::ostream& os, const Colors& colors,
                   const fidl_message_header_t* header, std::string_view line_header, int tabs,
                   int remaining_size, int max_line_size) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Bits& bits_;
};

class HandleType : public Type {
 public:
  HandleType() {}

  std::string Name() const override { return "handle"; }

  size_t InlineSize(bool unions_are_xunions) const override { return sizeof(zx_handle_t); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_TYPES_H_
