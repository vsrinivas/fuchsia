// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_VALUE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_VALUE_H_

#include <cstdint>
#include <string>

#include "src/lib/syslog/cpp/logger.h"

namespace shell {
namespace interpreter {

template <typename Type, typename ContainerType>
class Container;

class Thread;

enum class ValueType {
  // Value is not defined. This is, for example, the case when we try to load a global which doesn't
  // exist.
  kUndef,
  // The value is a 8 bit signed integer.
  kInt8,
  // The value is a 8 bit unsigned integer.
  kUint8,
  // The value is a 16 bit signed integer.
  kInt16,
  // The value is a 16 bit unsigned integer.
  kUint16,
  // The value is a 32 bit signed integer.
  kInt32,
  // The value is a 32 bit unsigned integer.
  kUint32,
  // The value is a 64 bit signed integer.
  kInt64,
  // The value is a 64 bit unsigned integer.
  kUint64,
  // The value is a string.
  kString,
};

// Base class for all reference counted objects.
class ReferenceCountedBase {
  friend class ExecutionScope;
  friend void StringConcatenation(Thread* thread, uint64_t count);

 protected:
  ReferenceCountedBase() = default;
  ReferenceCountedBase(const ReferenceCountedBase&) = delete;
  ReferenceCountedBase(ReferenceCountedBase&&) = delete;
  virtual ~ReferenceCountedBase() = default;

  ReferenceCountedBase& operator=(const ReferenceCountedBase&) = delete;
  ReferenceCountedBase& operator=(ReferenceCountedBase&&) = delete;

 protected:
  // Adds a reference to this value.
  void Use() {
    // reference_count_ is initalized at one (the reference for the creator). That means it can
    // never be zero.
    FX_DCHECK(reference_count_ > 0);
    ++reference_count_;
  }

  // Releases a reference to this value. When the count is zero, the value is destroyed.
  void Release() {
    FX_DCHECK(reference_count_ > 0);
    if (--reference_count_ == 0) {
      delete this;
    }
  }

  // Reference count for the value. When the count reaches zero, the value is destroyed.
  size_t reference_count_ = 1;
};

// Base class for all reference counted objects.
template <typename Type>
class ReferenceCounted : public ReferenceCountedBase {
  template <typename ObjectType, typename ContainerType>
  friend class Container;

  friend class Value;

 protected:
  ReferenceCounted() = default;
  ReferenceCounted(const ReferenceCounted<Type>&) = delete;
  ReferenceCounted(ReferenceCounted<Type>&&) = delete;

  ReferenceCounted<Type>& operator=(const ReferenceCounted<Type>&) = delete;
  ReferenceCounted<Type>& operator=(ReferenceCounted<Type>&&) = delete;

 private:
  // Adds a reference to this value.
  Type* Use() {
    ReferenceCountedBase::Use();
    return reinterpret_cast<Type*>(this);
  }

  // Reference count for the value. When the count reaches zero, the value is destroyed.
  size_t reference_count_ = 1;
};

// Helper class for reference counted objects. This automatically manages the references.
template <typename Type, typename ContainerType>
class Container {
 protected:
  explicit Container(Type* data) : data_(data->Use()) {}
  Container(const ContainerType& from) : data_(from.data_->Use()) {}
  Container(ContainerType&& from) : data_(std::move(from.data_)) {}
  ~Container() { data_->Release(); }

 public:
  void operator=(const ContainerType& from) {
    Type* data = from->data_->Use();
    data_->Release();
    data_ = data;
  }

  void operator=(ContainerType&& from) { data_ = std::move(from->data_); }

  Type* data() const { return data_; }

 private:
  Type* data_;
};

// Defines a string. The value is not mutable.
class String : public ReferenceCounted<String> {
 public:
  explicit String(std::string_view value) : value_(value) {}
  explicit String(std::string&& value) : value_(std::move(value)) {}

  const std::string& value() const { return value_; }
  size_t size() const { return value_.size(); }

 private:
  const std::string value_;
};

// Helper class for strings. This automatically manages the references.
class StringContainer : public Container<String, StringContainer> {
 public:
  explicit StringContainer(std::string_view value) : Container(new String(value)) {}
  explicit StringContainer(String* string) : Container(string) {}
  StringContainer(const StringContainer& from) : Container(from) {}
  StringContainer(StringContainer&& from) : Container(from) {}
};

// Stores any value manageable by the interpreter. This is used when something has an undefined
// type. That means that we can assign any type of value to it (integer, string, ...).
// Currently, it's used when the client asks for the value of a global.
class Value {
 public:
  Value() = default;
  ~Value() { Release(); }

  ValueType type() const { return type_; }

  int64_t GetInt8() const {
    FX_DCHECK(type_ == ValueType::kInt8);
    return int8_value_;
  }
  void SetInt8(int8_t value) {
    Release();
    type_ = ValueType::kInt8;
    int8_value_ = value;
  }

  uint8_t GetUint8() const {
    FX_DCHECK(type_ == ValueType::kUint8);
    return uint8_value_;
  }
  void SetUint8(uint8_t value) {
    Release();
    type_ = ValueType::kUint8;
    uint8_value_ = value;
  }

  int16_t GetInt16() const {
    FX_DCHECK(type_ == ValueType::kInt16);
    return int16_value_;
  }
  void SetInt16(int16_t value) {
    Release();
    type_ = ValueType::kInt16;
    int16_value_ = value;
  }

  uint16_t GetUint16() const {
    FX_DCHECK(type_ == ValueType::kUint16);
    return uint16_value_;
  }
  void SetUint16(uint16_t value) {
    Release();
    type_ = ValueType::kUint16;
    uint16_value_ = value;
  }

  int32_t GetInt32() const {
    FX_DCHECK(type_ == ValueType::kInt32);
    return int32_value_;
  }
  void SetInt32(int32_t value) {
    Release();
    type_ = ValueType::kInt32;
    int32_value_ = value;
  }

  uint32_t GetUint32() const {
    FX_DCHECK(type_ == ValueType::kUint32);
    return uint32_value_;
  }
  void SetUint32(uint32_t value) {
    Release();
    type_ = ValueType::kUint32;
    uint32_value_ = value;
  }

  int64_t GetInt64() const {
    FX_DCHECK(type_ == ValueType::kInt64);
    return int64_value_;
  }
  void SetInt64(int64_t value) {
    Release();
    type_ = ValueType::kInt64;
    int64_value_ = value;
  }

  uint64_t GetUint64() const {
    FX_DCHECK(type_ == ValueType::kUint64);
    return uint64_value_;
  }
  void SetUint64(uint64_t value) {
    Release();
    type_ = ValueType::kUint64;
    uint64_value_ = value;
  }

  String* GetString() const {
    FX_DCHECK(type_ == ValueType::kString);
    return string_;
  }
  void SetString(std::string_view value) {
    // Creates the new value before releasing the old one to avoid potential use after free problem.
    String* string = new String(value);
    Release();
    type_ = ValueType::kString;
    string_ = string;
  }
  void SetString(String* value) {
    // Take a new reference to the value before releasing the old one to avoid potential use after
    // free problem.
    String* string = value->Use();
    Release();
    type_ = ValueType::kString;
    string_ = string;
  }

  void Set(const Value& value);

 private:
  // Release the data for this value. This is used when the value is destroyed or when is value is
  // modified.
  void Release();

  // Current type for the value.
  ValueType type_ = ValueType::kUndef;
  union {
    // Integer value when type is kInt8.
    int8_t int8_value_;
    // Integer value when type is kUint8.
    uint8_t uint8_value_;
    // Integer value when type is kInt16.
    int16_t int16_value_;
    // Integer value when type is kUint16.
    uint16_t uint16_value_;
    // Integer value when type is kInt32.
    int32_t int32_value_;
    // Integer value when type is kUint32.
    uint32_t uint32_value_;
    // Integer value when type is kInt64.
    int64_t int64_value_;
    // Integer value when type is kUint64.
    uint64_t uint64_value_;
    // String value when type is kString.
    String* string_;
  };
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_VALUE_H_
