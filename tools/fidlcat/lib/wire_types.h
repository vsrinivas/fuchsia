// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_WIRE_TYPES_H_
#define TOOLS_FIDLCAT_LIB_WIRE_TYPES_H_

#include <lib/fit/function.h>

#if defined(__APPLE__)

#include <libkern/OSByteOrder.h>

#define le16toh(x) OSSwapLittleToHostInt16(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#else
#include <endian.h>
#endif

#ifdef __Fuchsia__
#include <zircon/types.h>
#else
typedef uint32_t zx_handle_t;
#endif

#include <sstream>
#include <string>
#include <vector>

#include "rapidjson/document.h"
#include "tools/fidlcat/lib/library_loader.h"

// This file contains classes that help parse FIDL messages as they come in off
// the wire.

namespace fidlcat {

// Indicates a position in the FIDL message, which is a combination of current
// location in the byte part |byte_pos| and current location in the handle part
// |handle_pos|.
class Marker {
 public:
  Marker() = delete;

  Marker(const uint8_t* bytes, const zx_handle_t* handles)
      : byte_pos_(bytes),
        handle_pos_(handles),
        end_byte_pos_(nullptr),
        end_handle_pos_(nullptr) {}

  Marker(const uint8_t* bytes, const zx_handle_t* handles, const Marker& end)
      : byte_pos_(bytes),
        handle_pos_(handles),
        end_byte_pos_(end.byte_pos()),
        end_handle_pos_(end.handle_pos()) {}

  const uint8_t* byte_pos() const { return byte_pos_; }
  const zx_handle_t* handle_pos() const { return handle_pos_; }
  bool is_valid() const;

  // Advances the bytes in the given |marker| by the given |amount| (or to the
  // given |pos|).  Sets marker.is_valid to false if the new position would be
  // off the end of the tracked message.
  //
  // TODO(DX-1260): Consider folding all marker tracking into this class, so
  // that Markers and ObjectTrackers don't have to be passed around everywhere.
  void AdvanceBytesBy(size_t amount);
  void AdvanceBytesTo(const uint8_t* pos);
  void AdvanceHandlesBy(size_t amount);
  void AdvanceHandlesTo(const zx_handle_t* pos);

  std::string ToString() const;

 private:
  const uint8_t* byte_pos_;
  const zx_handle_t* handle_pos_;

  const uint8_t* end_byte_pos_;
  const zx_handle_t* end_handle_pos_;
};

class ObjectTracker;

// A function that, given the location |marker| of an object (in-line or
// out-of-line), generates a JSON representation into |value| using |allocator|.
//
// The |marker| is set to the out-of-line size of the object (which may be
// 0).
//
// The return value indicates whether the value generated should be included in
// the json.  It returns false, for example, for absent table members.
typedef fit::function<bool(ObjectTracker*, Marker& marker,
                           rapidjson::Value& value,
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
  ObjectTracker(const Marker& end) : end_(end) {}

  // Executes all of the callbacks, starting at bytes (as passed to the
  // constructor) + the given offset.
  bool RunCallbacksFrom(Marker& marker);

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

  const Marker& end() { return end_; }

 private:
  const Marker end_;
  std::vector<fit::function<void(Marker& marker)>> callbacks_;
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

  // Takes a Marker |marker| and length of the data part |length|, and
  // tells |tracker| to invoke |callback| on their representation given this
  // type.
  //
  // A callback may outlive the Type that provided it.  They should therefore
  // not refer to anything in the containing type, as that might get deleted.
  //
  // Returns a Marker pointing to the next element inline in this type.
  // marker.is_valid is set to false and otherwise unchanged from |marker| if
  // there was something wrong with the data (e.g., this method would have had
  // to read off the end of the data to parse it).
  virtual Marker GetValueCallback(Marker marker, size_t length,
                                  ObjectTracker* tracker,
                                  ValueGeneratingCallback& callback) const = 0;

  // Takes a Marker |marker| and length of the data part |length|, and
  // returns whether that is equal to the Value represented by |value| according
  // to this type.
  virtual bool ValueEquals(Marker marker, size_t length,
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
  UnknownType(size_t inline_size) : inline_size_(inline_size) {}

  UnknownType() : inline_size_(0) {}

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override { return inline_size_; }

 private:
  size_t inline_size_;
};

class StringType : public Type {
 public:
  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
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

template <typename T>
T LeToHost<T>::le_to_host(const T* bytes) {
  if constexpr (std::is_same<T, uint8_t>::value) {
    return *bytes;
  } else if constexpr (std::is_same<T, uint16_t>::value) {
    return le16toh(*bytes);
  } else if constexpr (std::is_same<T, uint32_t>::value) {
    return le32toh(*bytes);
  } else if constexpr (std::is_same<T, uint64_t>::value) {
    return le64toh(*bytes);
  } else if constexpr (std::is_same<T, uintptr_t>::value &&
                       sizeof(T) == sizeof(uint64_t)) {
    // NB: On Darwin, uintptr_t and uint64_t are different things.
    return le64toh(*bytes);
  }
}

template <typename T>
struct GetUnsigned {
  using type = typename std::conditional<std::is_same<float, T>::value,
                                         uint32_t, uint64_t>::type;
};

template <typename T, typename P>
T MemoryFrom(P bytes) {
  static_assert(std::is_pointer<P>::value,
                "MemoryFrom can only be used on pointers");
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
  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override {
    T val = internal::MemoryFrom<T, const uint8_t*>(marker.byte_pos());
    callback = [val](ObjectTracker* tracker, Marker& marker,
                     rapidjson::Value& value,
                     rapidjson::Document::AllocatorType& allocator) {
      value.SetString(std::to_string(val).c_str(), allocator);
      return true;
    };
    marker.AdvanceBytesBy(sizeof(T));
    return marker;
  }

  virtual bool ValueEquals(Marker marker, size_t length,
                           const rapidjson::Value& value) const override {
    T lhs = internal::MemoryFrom<T, const uint8_t*>(marker.byte_pos());
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
  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override { return sizeof(uint8_t); }
};

class StructType : public Type {
 public:
  StructType(const Struct& str) : struct_(str) {}

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override;

 private:
  const Struct& struct_;
};

class TableType : public Type {
 public:
  TableType(const Table& tab) : table_(tab) {}

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override;

 private:
  const Table& table_;
};

class UnionType : public Type {
 public:
  UnionType(const Union& uni);

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override;

 private:
  const Union& union_;
};

// A type that can be used to express that this is a pointer to an instance of
// another type.
class PointerType : public Type {
 public:
  // target_type is the referent type, and keep_null indicates whether the value
  // callback returns true or false when the pointer is null.
  explicit PointerType(Type* target_type, bool keep_null = true);

  explicit PointerType(std::shared_ptr<Type> target_type,
                       bool keep_null = true);

  // PointerType's GetValueCallback method does the following:
  //
  // a) In the case where the intptr at the marker is null, returns a callback
  //    that sets the value to null.  This callback returns keep_null_.
  // b) In the case where the intptr at the marker is not null, returns a
  //    callback that tracks an instance of the wrapped type out-of-line,
  //    with its own ObjectTracker.
  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override { return sizeof(uint64_t); }

 private:
  std::shared_ptr<Type> target_type_;
  bool keep_null_;
};

class ElementSequenceType : public Type {
 public:
  explicit ElementSequenceType(std::unique_ptr<Type>&& component_type);

 protected:
  explicit ElementSequenceType(std::shared_ptr<Type> component_type);

  // |tracker| is the ObjectTracker to use for the callback.
  // |count| is the number of elements in this sequence
  // |marker| is a position in the message we're decoding
  // |length| is the length of the byte sequence.
  ValueGeneratingCallback GetIteratingCallback(ObjectTracker* tracker,
                                               size_t count, Marker marker,
                                               size_t length) const;

  // The unique_ptr is converted to a shared_ptr so that it can be used by the
  // callback returned by GetValueCallback, which may outlive the
  // ElementSequenceType instance.
  std::shared_ptr<Type> component_type_;
};

class ArrayType : public ElementSequenceType {
 public:
  ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count);

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
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

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
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

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override { return enum_.size(); }

 private:
  const Enum& enum_;
};

class HandleType : public Type {
 public:
  HandleType() {}

  virtual Marker GetValueCallback(
      Marker marker, size_t length, ObjectTracker* tracker,
      ValueGeneratingCallback& callback) const override;

  virtual size_t InlineSize() const override { return sizeof(zx_handle_t); }
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

#endif  // TOOLS_FIDLCAT_LIB_WIRE_TYPES_H_
