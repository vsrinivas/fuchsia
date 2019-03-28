// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FIDLCAT_LIB_LIBRARY_LOADER_H_
#define GARNET_BIN_FIDLCAT_LIB_LIBRARY_LOADER_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fit/function.h>

#include <endian.h>

#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

#include "rapidjson/document.h"

// This file contains a programmatic representation of a FIDL schema.  A
// LibraryLoader loads a set of Libraries.  The libraries contain structs,
// enums, interfaces, and so on.  Each element has the logic necessary to take
// wire-encoded bits of that type, and transform it to a JSON representation of
// that type.

// A LibraryLoader object can be used to fetch a particular library or interface
// method, which can then be used for debug purposes.

// An example of building a LibraryLoader can be found in
// library_loader_test.cc:LoadSimple. Callers can then do something like the
// following, if they have a fidl::Message:
//
// fidl_message_header_t header = message.header();
// const InterfaceMethod* method;
// loader_->GetByOrdinal(header.ordinal, &method);
// rapidjson::Document actual;
// fidlcat::RequestToJSON(method, message, actual);
//
// |actual| will then contain the contents of the message in JSON
// (human-readable) format.
//
// These libraries are currently thread-unsafe.

namespace fidlcat {

typedef uint32_t Ordinal;

struct LibraryReadError {
  enum ErrorValue {
    kOk,
    kIoError,
    kParseError,
  };
  ErrorValue value;
  rapidjson::ParseResult parse_result;
};

class Library;
class LibraryLoader;
class Interface;
class InterfaceMethod;
class Enum;
class Struct;

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

class InterfaceMethodParameter {
  friend class InterfaceMethod;

 public:
  InterfaceMethodParameter(const InterfaceMethod& enclosing_method,
                           const rapidjson::Value& value)
      : enclosing_method_(enclosing_method), value_(value) {}

  InterfaceMethodParameter(const InterfaceMethodParameter&) = default;
  InterfaceMethodParameter& operator=(const InterfaceMethodParameter&) = delete;

  uint64_t get_offset() const {
    return std::strtoll(value_["offset"].GetString(), nullptr, 10);
  }

  uint64_t get_size() const {
    return std::strtoll(value_["size"].GetString(), nullptr, 10);
  }

  std::string name() const { return value_["name"].GetString(); }

  std::unique_ptr<Type> GetType() const;

 private:
  const InterfaceMethod& enclosing_method_;
  const rapidjson::Value& value_;
};

class InterfaceMethod {
 public:
  InterfaceMethod(const Interface& interface, const rapidjson::Value& value)
      : enclosing_interface_(interface), value_(value) {
    if (value_["has_request"].GetBool()) {
      request_params_ =
          std::make_optional<std::vector<InterfaceMethodParameter>>();
      for (auto& request : value["maybe_request"].GetArray()) {
        request_params_->emplace_back(*this, request);
      }
    } else {
      request_params_ = {};
    }

    if (value_["has_response"].GetBool()) {
      response_params_ =
          std::make_optional<std::vector<InterfaceMethodParameter>>();
      for (auto& response : value["maybe_response"].GetArray()) {
        response_params_->emplace_back(*this, response);
      }
    } else {
      response_params_ = {};
    }
  }

  InterfaceMethod(InterfaceMethod&& other)
      : enclosing_interface_(other.enclosing_interface_), value_(other.value_) {
    if (other.request_params_) {
      request_params_ =
          std::make_optional<std::vector<InterfaceMethodParameter>>();
      for (auto& request : *other.request_params_) {
        request_params_->emplace_back(*this, request.value_);
      }
    } else {
      request_params_ = {};
    }

    if (other.response_params_) {
      response_params_ =
          std::make_optional<std::vector<InterfaceMethodParameter>>();
      for (auto& response : *other.response_params_) {
        response_params_->emplace_back(*this, response.value_);
      }
    } else {
      response_params_ = {};
    }
  }

  int32_t get_ordinal() const {
    return std::strtoll(value_["ordinal"].GetString(), nullptr, 10);
  }

  std::string name() const { return value_["name"].GetString(); }

  std::string fully_qualified_name() const;

  const std::optional<std::vector<InterfaceMethodParameter>>& request_params()
      const {
    return request_params_;
  }

  const std::optional<uint64_t> request_size() const {
    if (value_.HasMember("maybe_request_size")) {
      return std::strtoll(value_["maybe_request_size"].GetString(), nullptr,
                          10);
    }
    return {};
  }

  const std::optional<std::vector<InterfaceMethodParameter>>& response_params()
      const {
    return response_params_;
  }

  const Interface& enclosing_interface() const { return enclosing_interface_; }

  InterfaceMethod(const InterfaceMethod& other) = delete;
  InterfaceMethod& operator=(const InterfaceMethod&) = delete;

 private:
  const Interface& enclosing_interface_;
  std::optional<std::vector<InterfaceMethodParameter>> request_params_;
  std::optional<std::vector<InterfaceMethodParameter>> response_params_;
  const rapidjson::Value& value_;
};

class Interface {
 public:
  Interface(const Library& library, const rapidjson::Value& value)
      : value_(value), enclosing_library_(library) {
    for (auto& method : value["methods"].GetArray()) {
      interface_methods_.emplace_back(*this, method);
    }
  }

  std::string name() const { return value_["name"].GetString(); }

  void AddMethodsToIndex(std::map<Ordinal, const InterfaceMethod*>& index) {
    for (size_t i = 0; i < interface_methods_.size(); i++) {
      const InterfaceMethod* method = &interface_methods_[i];
      Ordinal ordinal = method->get_ordinal();
      index[ordinal] = method;
    }
  }

  const Library& enclosing_library() const { return enclosing_library_; }

  const std::vector<InterfaceMethod>& methods() const {
    return interface_methods_;
  }

 private:
  const rapidjson::Value& value_;
  const Library& enclosing_library_;
  std::vector<InterfaceMethod> interface_methods_;
};

class Enum {
 public:
  Enum(const Library& enclosing_library, const rapidjson::Value& value)
      : enclosing_library_(enclosing_library), value_(value) {
    (void)value_;
    (void)enclosing_library_;
  }

  const std::unique_ptr<Type> GetType() const {
    // TODO Consider caching this.
    return Type::ScalarTypeFromName(value_["type"].GetString());
  }

  // Gets the name of the enum member corresponding to the value pointed to by
  // |bytes| of length |length|.  For example, if we had the following
  // definition:
  // enum i16_enum : int16 {
  //   x = -23;
  // };
  // and you pass |bytes| a 2-byte representation of -23, and |length| 2, this
  // function will return "x".  Returns "(Unknown enum member)" if it can't find
  // the member.
  std::string GetNameFromBytes(const uint8_t* bytes, size_t length) const {
    std::unique_ptr<Type> type = GetType();
    for (auto& member : value_["members"].GetArray()) {
      if (type->ValueEquals(bytes, length, member["value"]["literal"])) {
        return member["name"].GetString();
      }
    }
    return "(Unknown enum member)";
  }

  uint32_t size() const { return GetType()->InlineSize(); }

 private:
  const Library& enclosing_library_;
  const rapidjson::Value& value_;
};

class StructMember {
 public:
  StructMember(const Struct& enclosing_struct, const rapidjson::Value& value)
      : enclosing_struct_(enclosing_struct), value_(value) {
    // TODO: delete the following line
    (void)value_;
  }

  std::unique_ptr<Type> GetType() const;

  uint64_t size() const {
    return std::strtoll(value_["size"].GetString(), nullptr, 10);
  }

  uint64_t offset() const {
    return std::strtoll(value_["offset"].GetString(), nullptr, 10);
  }

  std::string name() const { return value_["name"].GetString(); }

  const Struct& enclosing_struct() const { return enclosing_struct_; }

 private:
  const Struct& enclosing_struct_;
  const rapidjson::Value& value_;
};

class Struct {
  friend class Library;

 public:
  Struct(const Library& enclosing_library, const rapidjson::Value& value)
      : enclosing_library_(enclosing_library), value_(value) {
    // TODO: delete the next line.
    (void)value_;
    (void)enclosing_library_;
    for (auto& member : value["members"].GetArray()) {
      members_.emplace_back(*this, member);
    }
  }

  const Library& enclosing_library() const { return enclosing_library_; }

  const std::vector<StructMember>& members() const { return members_; }

  uint32_t size() const {
    return std::strtoll(value_["size"].GetString(), nullptr, 10);
  }

 private:
  const rapidjson::Value& schema() const { return value_; }

  const Library& enclosing_library_;
  const rapidjson::Value& value_;
  std::vector<StructMember> members_;
};

class Library {
 public:
  Library(const LibraryLoader& enclosing, rapidjson::Document& document)
      : enclosing_loader_(enclosing), backing_document_(std::move(document)) {
    for (auto& decl : backing_document_["interface_declarations"].GetArray()) {
      interfaces_.emplace_back(*this, decl);
    }
    for (auto& enu : backing_document_["enum_declarations"].GetArray()) {
      enums_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(enu["name"].GetString()),
                     std::forward_as_tuple(*this, enu));
    }
    for (auto& str : backing_document_["struct_declarations"].GetArray()) {
      structs_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(str["name"].GetString()),
                       std::forward_as_tuple(*this, str));
    }
  }

  // Adds methods to this Library.  Pass it a std::map from ordinal value to the
  // InterfaceMethod represented by that ordinal.
  void AddMethodsToIndex(std::map<Ordinal, const InterfaceMethod*>& index) {
    for (size_t i = 0; i < interfaces_.size(); i++) {
      interfaces_[i].AddMethodsToIndex(index);
    }
  }

  std::string name() { return backing_document_["name"].GetString(); }

  const LibraryLoader& enclosing_loader() const { return enclosing_loader_; }

  std::unique_ptr<Type> TypeFromIdentifier(bool is_nullable,
                                           std::string& identifier) const;

  // The size of the type with name |identifier| when it is inline (e.g.,
  // embedded in an array)
  size_t InlineSizeFromIdentifier(std::string& identifier) const;

  const std::vector<Interface>& interfaces() const { return interfaces_; }

  Library& operator=(const Library&) = delete;
  Library(const Library&) = delete;

 private:
  const LibraryLoader& enclosing_loader_;
  rapidjson::Document backing_document_;

  std::vector<Interface> interfaces_;
  std::map<std::string, Enum> enums_;
  std::map<std::string, Struct> structs_;
};

// An indexed collection of libraries.
class LibraryLoader {
 public:
  LibraryLoader(std::vector<std::unique_ptr<std::istream>>& library_streams,
                LibraryReadError* err);

  LibraryLoader& operator=(const LibraryLoader&) = delete;
  LibraryLoader(const LibraryLoader&) = delete;

  // Returns true and sets **method if the ordinal was present in the map, and
  // false otherwise.
  bool GetByOrdinal(Ordinal ordinal, const InterfaceMethod** method) {
    auto m = ordinal_map_.find(ordinal);
    if (m != ordinal_map_.end()) {
      *method = m->second;
      return true;
    }
    return false;
  }

  // If the library with name |name| is present in this loader, sets *|library|
  // to a pointer to that library and returns true.  Otherwise, returns false.
  // |name| is of the format "a.b.c"
  bool GetLibraryFromName(const std::string& name,
                          const Library** library) const {
    auto l = representations_.find(name);
    if (l != representations_.end()) {
      const Library* lib = &(l->second);
      *library = lib;
      return true;
    }
    return false;
  }

 private:
  void Add(std::string& ir, LibraryReadError* err) {
    rapidjson::Document document;
    err->parse_result =
        document.Parse<rapidjson::kParseNumbersAsStringsFlag>(ir.c_str());
    // TODO: This would be a good place to validate that the resulting JSON
    // matches the schema in zircon/system/host/fidl/schema.json.  If there are
    // errors, we will currently get mysterious crashes.
    if (document.HasParseError()) {
      err->value = LibraryReadError::kParseError;
      return;
    }
    std::string library_name = document["name"].GetString();
    representations_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(library_name),
                             std::forward_as_tuple(*this, document));
    auto i = representations_.find(library_name);
    i->second.AddMethodsToIndex(ordinal_map_);
  }

  std::map<std::string, Library> representations_;
  std::map<Ordinal, const InterfaceMethod*> ordinal_map_;
};

}  // namespace fidlcat

#endif  // GARNET_BIN_FIDLCAT_LIB_LIBRARY_LOADER_H_
