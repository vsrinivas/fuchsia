// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FIDLCAT_LIB_WIRE_TYPES_H_
#define GARNET_BIN_FIDLCAT_LIB_WIRE_TYPES_H_

#include <lib/fit/function.h>

#include <sstream>
#include <string>
#include <vector>

#include "garnet/bin/fidlcat/lib/library_loader.h"
#include "rapidjson/document.h"

// This file contains classes that help parse FIDL messages as they come in off
// the wire.

namespace fidlcat {

// A function that, given the location |bytes| of an object (in-line or
// out-of-line), generates a JSON representation into |value| using |allocator|.
//
// Returns the out-of-line size, or 0 if it was in-line element.
typedef fit::function<size_t(const uint8_t* bytes, rapidjson::Value& value,
                             rapidjson::Document::AllocatorType& allocator)>
    ValueGeneratingCallback;

// Encapsulates state when parsing wire format encoded FIDL objects.
//
// For each element a print function encounters on its walk through a
// fixed-length FIDL object, it enqueues a callback to be executed when the end
// of that element is reached.  If the element is an out-of-line object, it will
// parse the out-of-line object and return the value, or simply return the
// (captured) in line value.
class ObjectTracker {
 public:
  // Creates a tracker for the given array of bytes.
  explicit ObjectTracker(const uint8_t* bytes) : bytes_(bytes) {}

  // Executes all of the callbacks, starting at bytes (as passed to the
  // constructor) + the given offset.
  size_t RunCallbacksFrom(size_t offset);

  // Enqueues a callback to be executed when running RunCallbacksFrom.
  // |key| is the JSON key it will construct.
  // |callback| is the callback to execute to construct the value.
  // |target_object| is the place to put the key, value pair.
  // |allocator| is the correct allocator for that object.
  void ObjectEnqueue(const std::string& key, ValueGeneratingCallback&& callback,
                     rapidjson::Value& target_object,
                     rapidjson::Document::AllocatorType& allocator);

  // Enqueues a callback to be executed when running RunCallbacksFrom.
  // |callback| is the callback to execute to construct the value.
  // |target_array| is the array in which to insert the value.
  // |allocator| is the correct allocator for that object.
  void ArrayEnqueue(ValueGeneratingCallback&& callback,
                    rapidjson::Value& target_array,
                    rapidjson::Document::AllocatorType& allocator);

  ObjectTracker(const ObjectTracker&) = delete;
  ObjectTracker& operator=(const ObjectTracker&) = delete;

 private:
  std::vector<fit::function<size_t(const uint8_t* bytes)>> callbacks_;
  const uint8_t* bytes_;
};

// A FIDL type.  Provides methods for generating instances of this type.
//
// TODO: This may be misnamed.  Right now, it's mostly only useful for
// generating JSON values and comparing instances for equality.  Consider
// removing it from this file or making it more generic (less tied to writing
// out JSON).
class Type {
  friend class InterfaceMethodParameter;
  friend class Library;

 public:
  Type() {}
  virtual ~Type() {}

  // Takes a series of bytes pointed to by |bytes| and of length |length|, and
  // tells |tracker| to invoke |callback| on their representation given this
  // type.
  //
  // A callback may outlive the Type that provided it.  They should therefore
  // not refer to anything in the containing type, as that might get deleted.
  virtual size_t GetValueCallback(const uint8_t* bytes, size_t length,
                                  ObjectTracker* tracker,
                                  ValueGeneratingCallback& callback) const = 0;

  // Takes a series of bytes pointed to by |bytes| and of length |length|, and
  // returns whether that is equal to the Value represented by |value| according
  // to this type.
  virtual bool ValueEquals(const uint8_t* bytes, size_t length,
                           const rapidjson::Value& value) const;

  // Returns the size of this type when embedded in another object.
  virtual size_t InlineSize() const;

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type (e.g., "array", "vector", "foo.bar/Baz").
  // |loader| is the set of libraries to use to find types that need to be given
  // by identifier (e.g., "foo.bar/Baz").
  static std::unique_ptr<Type> GetType(const LibraryLoader& loader,
                                       const rapidjson::Value& type);

  // Gets a Type object representing the |type|.  |type| is a JSON object with a
  // "subtype" field that represents a scalar type (e.g., "float64", "uint32")
  static std::unique_ptr<Type> TypeFromPrimitive(const rapidjson::Value& type);

  // Gets a Type object representing the |type_name|.  |type| is a string that
  // represents a scalar type (e.g., "float64", "uint32").
  static std::unique_ptr<Type> ScalarTypeFromName(const std::string& type_name);

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type.  "kind" is an identifier
  // (e.g.,"foo.bar/Baz").  |loader| is the set of libraries to use to lookup
  // that identifier.
  static std::unique_ptr<Type> TypeFromIdentifier(const LibraryLoader& loader,
                                                  const rapidjson::Value& type);

  static std::unique_ptr<Type> get_illegal();

  Type& operator=(const Type& other) = default;
  Type(const Type& other) = default;
};

// An instance of this class is created when the system can't determine the real
// class (e.g., in cases of corrupted metadata).  The ValueGeneratingCallback
// spits back hex pairs.
class UnknownType : public Type {
 public:
  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;
};

class StringType : public Type {
 public:
  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override {
    return sizeof(uint64_t) + sizeof(uint64_t);
  }
};

namespace internal {

// These are convenience functions for reading little endian (i.e., FIDL wire
// format encoded) bits.
template <typename T>
class LeToHost {
 public:
  static T le_to_host(const T* ts);
};

template <>
uint8_t LeToHost<uint8_t>::le_to_host(const uint8_t* bytes);

template <>
uint16_t LeToHost<uint16_t>::le_to_host(const uint16_t* bytes);

template <>
uint32_t LeToHost<uint32_t>::le_to_host(const uint32_t* bytes);

template <>
uint64_t LeToHost<uint64_t>::le_to_host(const uint64_t* bytes);

template <typename T>
struct GetUnsigned {
  using type = typename std::conditional<std::is_same<float, T>::value,
                                         uint32_t, uint64_t>::type;
};

template <typename T>
T MemoryFrom(const uint8_t* bytes) {
  using U = typename std::conditional<std::is_integral<T>::value,
                                      std::make_unsigned<T>,
                                      GetUnsigned<T>>::type::type;
  union {
    U uval;
    T tval;
  } u;
  u.uval = LeToHost<U>::le_to_host(reinterpret_cast<const U*>(bytes));
  return u.tval;
}

}  // namespace internal

// A generic type that can be used for any numeric value that corresponds to a
// C++ arithmetic value.
template <typename T>
class NumericType : public Type {
  static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value,
                "NumericType can only be used for numerics");

 public:
  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override {
    T val = internal::MemoryFrom<T>(bytes);
    callback = [val](const uint8_t* bytes, rapidjson::Value& value,
                     rapidjson::Document::AllocatorType& allocator) {
      value.SetString(std::to_string(val).c_str(), allocator);
      return 0;
    };
    return sizeof(T);
  }

  virtual bool ValueEquals(const uint8_t* bytes, size_t length,
                           const rapidjson::Value& value) const override {
    T lhs = internal::MemoryFrom<T>(bytes);
    std::istringstream input(value["value"].GetString());
    // Because int8_t is really char, and we don't want to read that.
    using R =
        typename std::conditional<std::is_same<T, int8_t>::value, int, T>::type;
    R rhs;
    input >> rhs;
    return lhs == rhs;
  }

  virtual size_t InlineSize() const override { return sizeof(T); }
};

class BoolType : public Type {
 public:
  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;
};

class StructType : public Type {
 public:
  StructType(const Struct& str, bool is_nullable);

  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

 private:
  const Struct& struct_;
  const bool is_nullable_;
};

class ElementSequenceType : public Type {
 public:
  explicit ElementSequenceType(std::unique_ptr<Type>&& component_type);

 protected:
  explicit ElementSequenceType(std::shared_ptr<Type> component_type);

  // |tracker| is the ObjectTracker to use for the callback.
  // |count| is the number of elements in this sequence
  // |bytes| is a pointer to the byte sequence we're decoding.
  // |length| is the length of the byte sequence.
  ValueGeneratingCallback GetIteratingCallback(ObjectTracker* tracker,
                                               size_t count,
                                               const uint8_t* bytes,
                                               size_t length) const;

  // The unique_ptr is converted to a shared_ptr so that it can be used by the
  // callback returned by GetValueCallback, which may outlive the
  // ElementSequenceType instance.
  std::shared_ptr<Type> component_type_;
};

class ArrayType : public ElementSequenceType {
 public:
  ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count);

  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override {
    return component_type_->InlineSize() * count_;
  }

 private:
  uint32_t count_;
};

class VectorType : public ElementSequenceType {
 public:
  VectorType(std::unique_ptr<Type>&& component_type);

  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override {
    return sizeof(uint64_t) + sizeof(uint64_t);
  }

 private:
  VectorType(std::shared_ptr<Type> component_type, size_t element_size);
};

class EnumType : public Type {
 public:
  EnumType(const Enum& e);

  virtual size_t GetValueCallback(
      const uint8_t* bytes, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

 private:
  const Enum& enum_;
};

using Float32Type = NumericType<float>;
using Float64Type = NumericType<double>;
using Int8Type = NumericType<int8_t>;
using Int16Type = NumericType<int16_t>;
using Int32Type = NumericType<int32_t>;
using Int64Type = NumericType<int64_t>;
using Uint8Type = NumericType<uint8_t>;
using Uint16Type = NumericType<uint16_t>;
using Uint32Type = NumericType<uint32_t>;
using Uint64Type = NumericType<uint64_t>;

}  // namespace fidlcat

#endif  // GARNET_BIN_FIDLCAT_LIB_WIRE_TYPES_H_
