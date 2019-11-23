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

// A FIDL type.  Provides methods for generating instances of this type.
class Type {
  friend class InterfaceMethodParameter;
  friend class Library;

 public:
  Type() = default;
  virtual ~Type() = default;

  // Return true if the type is a RawType.
  virtual bool IsRaw() const { return false; }

  // Return a readable representation of the type.
  virtual std::string Name() const = 0;

  // Takes a pointer |bytes| and length of the data part |length|, and
  // returns whether that is equal to the Value represented by |value| according
  // to this type.
  virtual bool ValueEquals(const uint8_t* bytes, size_t length,
                           const rapidjson::Value& value) const;

  // Takes a pointer |bytes| and length of the data part |length|, and
  // returns whether that contains the Value represented by |value| according
  // to this type.
  virtual bool ValueHas(const uint8_t* bytes, const rapidjson::Value& value) const;

  // Returns the size of this type when embedded in another object.
  virtual size_t InlineSize(MessageDecoder* decoder) const;

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

  // Whether this is a nullable type.
  virtual bool Nullable() const { return false; }

  Type& operator=(const Type& other) = default;
  Type(const Type& other) = default;
};

// An instance of this class is created when the system can't determine the real
// class (e.g., in cases of corrupted metadata). Only a hexa dump is generated.
class RawType : public Type {
 public:
  explicit RawType(size_t inline_size) : inline_size_(inline_size) {}

  bool IsRaw() const override { return true; }

  std::string Name() const override { return "unknown"; }

  size_t InlineSize(MessageDecoder* decoder) const override { return inline_size_; }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  bool Nullable() const override { return true; }

 private:
  size_t inline_size_;
};

class StringType : public Type {
 public:
  std::string Name() const override { return "string"; }

  size_t InlineSize(MessageDecoder* decoder) const override {
    return sizeof(uint64_t) + sizeof(uint64_t);
  }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

  bool Nullable() const override { return true; }
};

// A generic type that can be used for any numeric value that corresponds to a
// C++ arithmetic value.
template <typename T>
class NumericType : public Type {
  static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value,
                "NumericType can only be used for numerics");

 public:
  virtual bool ValueEquals(const uint8_t* bytes, size_t length,
                           const rapidjson::Value& value) const override {
    T lhs = internal::MemoryFrom<T, const uint8_t*>(bytes);
    std::istringstream input(value["value"].GetString());
    if (sizeof(T) == 1) {
      // Because int8_t and uint8_t are really char, and we don't want to read that.
      int rhs;
      input >> rhs;
      return lhs == rhs;
    }
    T rhs;
    input >> rhs;
    return lhs == rhs;
  }

  size_t InlineSize(MessageDecoder* decoder) const override { return sizeof(T); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override {
    auto got = decoder->GetAddress(offset, sizeof(T));
    return std::make_unique<NumericValue<T>>(this, reinterpret_cast<const T*>(got));
  }
};

// A generic type that can be used for any integer numeric value that corresponds to a
// C++ integral value.
template <typename T>
class IntegralType : public NumericType<T> {
  static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value,
                "IntegralType can only be used for integers");

 public:
  bool ValueHas(const uint8_t* bytes, const rapidjson::Value& value) const override {
    if (!std::is_integral<T>::value) {
      return false;
    }
    T lhs = internal::MemoryFrom<T, const uint8_t*>(bytes);
    std::istringstream input(value["value"].GetString());
    if (sizeof(T) == 1) {
      // Because int8_t and uint8_t are really char, and we don't want to read that.
      int rhs;
      input >> rhs;
      return (lhs & rhs) == rhs;
    }
    T rhs;
    input >> rhs;
    return (lhs & rhs) == rhs;
  }
};

class Float32Type : public NumericType<float> {
 public:
  std::string Name() const override { return "float32"; }
};

class Float64Type : public NumericType<double> {
 public:
  std::string Name() const override { return "float64"; }
};

class Int8Type : public IntegralType<int8_t> {
 public:
  std::string Name() const override { return "int8"; }
};

class Int16Type : public IntegralType<int16_t> {
 public:
  std::string Name() const override { return "int16"; }
};

class Int32Type : public IntegralType<int32_t> {
 public:
  std::string Name() const override { return "int32"; }
};

class Int64Type : public IntegralType<int64_t> {
 public:
  std::string Name() const override { return "int64"; }
};

class Uint8Type : public IntegralType<uint8_t> {
 public:
  std::string Name() const override { return "uint8"; }
};

class Uint16Type : public IntegralType<uint16_t> {
 public:
  std::string Name() const override { return "uint16"; }
};

class Uint32Type : public IntegralType<uint32_t> {
 public:
  std::string Name() const override { return "uint32"; }
};

class Uint64Type : public IntegralType<uint64_t> {
 public:
  std::string Name() const override { return "uint64"; }
};

class BoolType : public Type {
 public:
  std::string Name() const override { return "bool"; }

  size_t InlineSize(MessageDecoder* decoder) const override { return sizeof(uint8_t); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;
};

class StructType : public Type {
 public:
  StructType(const Struct& str, bool nullable) : struct_(str), nullable_(nullable) {}

  bool Nullable() const override { return nullable_; }

  std::string Name() const override { return struct_.name(); }

  size_t InlineSize(MessageDecoder* decoder) const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

 private:
  const Struct& struct_;
  const bool nullable_;
};

class TableType : public Type {
 public:
  explicit TableType(const Table& tab) : table_(tab) {}

  std::string Name() const override { return table_.name(); }

  size_t InlineSize(MessageDecoder* decoder) const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

 private:
  const Table& table_;
};

class UnionType : public Type {
 public:
  UnionType(const Union& uni, bool nullable);

  std::string Name() const override { return union_.name(); }
  bool Nullable() const override { return nullable_; }

  size_t InlineSize(MessageDecoder* decoder) const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

 private:
  const Union& union_;
  const bool nullable_;
};

class XUnionType : public Type {
 public:
  XUnionType(const XUnion& uni, bool nullable);

  std::string Name() const override { return xunion_.name(); }
  bool Nullable() const override { return nullable_; }

  size_t InlineSize(MessageDecoder* decoder) const override;

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

 private:
  const XUnion& xunion_;
  const bool nullable_;
};

class ElementSequenceType : public Type {
 public:
  explicit ElementSequenceType(std::unique_ptr<Type>&& component_type);

 protected:
  std::unique_ptr<Type> component_type_;
};

class ArrayType : public ElementSequenceType {
 public:
  ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count);

  std::string Name() const override {
    return std::string("array<") + component_type_->Name() + ">";
  }

  size_t InlineSize(MessageDecoder* decoder) const override {
    return component_type_->InlineSize(decoder) * count_;
  }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

 private:
  uint32_t count_;
};

class VectorType : public ElementSequenceType {
 public:
  explicit VectorType(std::unique_ptr<Type>&& component_type);

  std::string Name() const override {
    return std::string("vector<") + component_type_->Name() + ">";
  }

  bool Nullable() const override { return true; }

  size_t InlineSize(MessageDecoder* decoder) const override {
    return sizeof(uint64_t) + sizeof(uint64_t);
  }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;
};

class EnumType : public Type {
 public:
  explicit EnumType(const Enum& e);

  std::string Name() const override { return enum_.name(); }

  size_t InlineSize(MessageDecoder* decoder) const override { return enum_.size(); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

 private:
  const Enum& enum_;
};

class BitsType : public Type {
 public:
  explicit BitsType(const Bits& b);

  std::string Name() const override { return bits_.name(); }

  size_t InlineSize(MessageDecoder* decoder) const override { return bits_.size(); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;

 private:
  const Bits& bits_;
};

class HandleType : public Type {
 public:
  HandleType() {}

  std::string Name() const override { return "handle"; }

  size_t InlineSize(MessageDecoder* decoder) const override { return sizeof(zx_handle_t); }

  std::unique_ptr<Value> Decode(MessageDecoder* decoder, uint64_t offset) const override;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_TYPES_H_
