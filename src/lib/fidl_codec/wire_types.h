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
#include "src/lib/fidl_codec/memory_helpers.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {

class LibraryLoader;
class TypeVisitor;

// A FIDL type.  Provides methods for generating instances of this type.
class Type {
  friend class InterfaceMethodParameter;
  friend class Library;

 public:
  Type() = default;
  virtual ~Type() = default;

  // Returns a detailed representation of the type.
  std::string ToString(bool expand = false) const;

  // Returns true if the type is a ArrayType.
  virtual bool IsArray() const { return false; }

  // Returns a readable representation of the type.
  virtual std::string Name() const = 0;

  // Returns the size of this type when embedded in another object.
  virtual size_t InlineSize() const = 0;

  // Whether this is a nullable type.
  virtual bool Nullable() const { return false; }

  // For vectors and arrays, give the type of the components (members).
  virtual const Type* GetComponentType() const { return nullptr; }

  // Decodes the type's inline part. It generates a Value and, eventually,
  // registers the field for further decoding (secondary objects).
  virtual std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const = 0;

  // Pretty prints the value for this type. This is used to print numerical values.
  virtual void PrettyPrint(const Value* value, PrettyPrinter& printer) const;

  // Use a visitor on this value;
  virtual void Visit(TypeVisitor* visitor) const = 0;

  // Gets a Type object representing the |type_name|.  |type| is a string that
  // represents a scalar type (e.g., "float64", "uint32").
  static std::unique_ptr<Type> ScalarTypeFromName(const std::string& type_name);

  // Gets a Type object representing the |type|.  |type| is a JSON object with a
  // "subtype" field that represents a scalar type (e.g., "float64", "uint32")
  static std::unique_ptr<Type> TypeFromPrimitive(const rapidjson::Value& type);

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type.  "kind" is an identifier
  // (e.g.,"foo.bar/Baz").  |loader| is the set of libraries to use to lookup
  // that identifier.
  static std::unique_ptr<Type> TypeFromIdentifier(LibraryLoader* loader,
                                                  const rapidjson::Value& type);

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type (e.g., "array", "vector", "foo.bar/Baz").
  // |loader| is the set of libraries to use to find types that need to be given
  // by identifier (e.g., "foo.bar/Baz").
  static std::unique_ptr<Type> GetType(LibraryLoader* loader, const rapidjson::Value& type);

  Type& operator=(const Type& other) = default;
  Type(const Type& other) = default;
};

// An instance of this class is created when the system can't determine the real
// class (e.g., in cases of corrupted metadata).
class InvalidType : public Type {
 public:
  InvalidType() = default;

  std::string Name() const override;

  size_t InlineSize() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

class BoolType : public Type {
 public:
  BoolType() = default;

  std::string Name() const override { return "bool"; }

  size_t InlineSize() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

// A generic type that can be used for any integer numeric value that corresponds to a
// C++ integral value.
template <typename T>
class IntegralType : public Type {
  static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value,
                "IntegralType can only be used for integers");

 public:
  explicit IntegralType(bool hexadecimal_display) : hexadecimal_display_(hexadecimal_display) {}

  size_t InlineSize() const override { return sizeof(T); }

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

  void PrettyPrint(const Value* value, PrettyPrinter& printer) const override {
    uint64_t absolute;
    bool negative;
    if (!value->GetIntegerValue(&absolute, &negative)) {
      printer << Red << "invalid" << ResetColor;
    } else {
      printer << Blue;
      if (negative) {
        printer << '-';
      }
      if (hexadecimal_display_) {
        printer << std::hex << absolute << std::dec << ResetColor;
      } else {
        printer << absolute << ResetColor;
      }
    }
  }

 private:
  bool hexadecimal_display_;
};

class Int8Type : public IntegralType<int8_t> {
 public:
  explicit Int8Type(bool hexadecimal_display = false) : IntegralType<int8_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Int16Type : public IntegralType<int16_t> {
 public:
  explicit Int16Type(bool hexadecimal_display = false)
      : IntegralType<int16_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Int32Type : public IntegralType<int32_t> {
 public:
  explicit Int32Type(bool hexadecimal_display = false)
      : IntegralType<int32_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Int64Type : public IntegralType<int64_t> {
 public:
  explicit Int64Type(bool hexadecimal_display = false)
      : IntegralType<int64_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Uint8Type : public IntegralType<uint8_t> {
 public:
  explicit Uint8Type(bool hexadecimal_display = false)
      : IntegralType<uint8_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Uint16Type : public IntegralType<uint16_t> {
 public:
  explicit Uint16Type(bool hexadecimal_display = false)
      : IntegralType<uint16_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Uint32Type : public IntegralType<uint32_t> {
 public:
  explicit Uint32Type(bool hexadecimal_display = false)
      : IntegralType<uint32_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Uint64Type : public IntegralType<uint64_t> {
 public:
  explicit Uint64Type(bool hexadecimal_display = false)
      : IntegralType<uint64_t>(hexadecimal_display) {}

  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

// A generic type that can be used for any numeric value that corresponds to a
// C++ arithmetic value.
template <typename T>
class NumericType : public Type {
  static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value,
                "NumericType can only be used for numerics");

 public:
  size_t InlineSize() const override { return sizeof(T); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override {
    auto got = decoder->GetAddress(offset, sizeof(T));
    if (got == nullptr) {
      return std::make_unique<InvalidValue>();
    }
    return std::make_unique<DoubleValue>(*reinterpret_cast<const T*>(got));
  }

  void PrettyPrint(const Value* value, PrettyPrinter& printer) const override {
    double result;
    if (!value->GetDoubleValue(&result)) {
      printer << Red << "invalid" << ResetColor;
    } else {
      printer << Blue << std::to_string(result) << ResetColor;
    }
  }
};

class Float32Type : public NumericType<float> {
 public:
  Float32Type() = default;
  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class Float64Type : public NumericType<double> {
 public:
  Float64Type() = default;
  std::string Name() const override;
  void Visit(TypeVisitor* visitor) const override;
};

class StringType : public Type {
 public:
  StringType() = default;

  std::string Name() const override;

  size_t InlineSize() const override;

  bool Nullable() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

class HandleType : public Type {
 public:
  HandleType() = default;

  std::string Name() const override;

  size_t InlineSize() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

class EnumType : public Type {
 public:
  explicit EnumType(const Enum& enum_definition) : enum_definition_(enum_definition) {}

  const Enum& enum_definition() const { return enum_definition_; }

  std::string Name() const override;

  size_t InlineSize() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void PrettyPrint(const Value* value, PrettyPrinter& printer) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Enum& enum_definition_;
};

class BitsType : public Type {
 public:
  explicit BitsType(const Bits& bits_definition) : bits_definition_(bits_definition) {}

  const Bits& bits_definition() const { return bits_definition_; }

  std::string Name() const override;

  size_t InlineSize() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void PrettyPrint(const Value* value, PrettyPrinter& printer) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Bits& bits_definition_;
};

class UnionType : public Type {
 public:
  UnionType(const Union& union_definition, bool nullable)
      : union_definition_(union_definition), nullable_(nullable) {}

  const Union& union_definition() const { return union_definition_; }

  std::string Name() const override;

  size_t InlineSize() const override;

  bool Nullable() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Union& union_definition_;
  const bool nullable_;
};

class StructType : public Type {
 public:
  StructType(const Struct& struct_definition, bool nullable)
      : struct_definition_(struct_definition), nullable_(nullable) {}

  const Struct& struct_definition() const { return struct_definition_; }

  std::string Name() const override;

  size_t InlineSize() const override;

  bool Nullable() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Struct& struct_definition_;
  const bool nullable_;
};

class ElementSequenceType : public Type {
 public:
  explicit ElementSequenceType(std::unique_ptr<Type> component_type)
      : component_type_(std::move(component_type)) {
    FXL_DCHECK(component_type_.get() != nullptr);
  }

  const Type* component_type() const { return component_type_.get(); }

  const Type* GetComponentType() const override;

  void Visit(TypeVisitor* visitor) const override;

 protected:
  std::unique_ptr<Type> component_type_;
};

class ArrayType : public ElementSequenceType {
 public:
  ArrayType(std::unique_ptr<Type> component_type, uint32_t count)
      : ElementSequenceType(std::move(component_type)), count_(count) {}

  uint32_t count() const { return count_; }

  bool IsArray() const override;

  std::string Name() const override;

  size_t InlineSize() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  uint32_t count_;
};

class VectorType : public ElementSequenceType {
 public:
  explicit VectorType(std::unique_ptr<Type> component_type)
      : ElementSequenceType(std::move(component_type)) {}

  std::string Name() const override;

  size_t InlineSize() const override;

  bool Nullable() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;
};

class TableType : public Type {
 public:
  explicit TableType(const Table& table_definition) : table_definition_(table_definition) {}

  const Table& table_definition() const { return table_definition_; }

  std::string Name() const override;

  size_t InlineSize() const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  void Visit(TypeVisitor* visitor) const override;

 private:
  const Table& table_definition_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_TYPES_H_
