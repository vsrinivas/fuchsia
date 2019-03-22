// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FIDLCAT_LIB_LIBRARY_LOADER_H_
#define GARNET_BIN_FIDLCAT_LIB_LIBRARY_LOADER_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fit/function.h>

#include <iostream>
#include <map>
#include <optional>
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

// Takes a series of bytes pointed to by |bytes| and of length |length|, and
// sets |value| to their representation for a particular type.  Uses |allocator|
// to allocate in |value|.  Returns the in-line length of data read.
typedef std::function<size_t(const uint8_t* bytes, size_t length,
                             ObjectTracker* tracker,
                             ValueGeneratingCallback& callback,
                             rapidjson::Document::AllocatorType& allocator)>
    PrintFunction;

// Takes a series of bytes pointed to by |bytes| and of length |length|, and
// returns whether that is equal to the Value represented by |value| according
// to some type.
typedef std::function<bool(const uint8_t* bytes, size_t length,
                           const rapidjson::Value& value)>
    EqualityFunction;

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
  // To avoid inheritance, a type takes two functions
  Type(PrintFunction printer, EqualityFunction equals)
      : printer_(printer), equals_(equals) {}

  // Takes a series of bytes pointed to by |bytes| and of length |length|, and
  // tells |tracker| to invoke |callback| on to their representation given this
  // type
  size_t MakeValue(const uint8_t* bytes, size_t length, ObjectTracker* tracker,
                   ValueGeneratingCallback& callback,
                   rapidjson::Document::AllocatorType& allocator) const {
    return printer_(bytes, length, tracker, callback, allocator);
  }

  // Takes a series of bytes pointed to by |bytes| and of length |length|, and
  // returns whether that is equal to the Value represented by |value| according
  // to this type.
  bool ValueEquals(const uint8_t* bytes, size_t length,
                   const rapidjson::Value& value) const {
    return equals_(bytes, length, value);
  }

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type (e.g., "array", "vector", "foo.bar/Baz").
  // |loader| is the set of libraries to use to find types that need to be given
  // by identifier (e.g., "foo.bar/Baz").
  static Type GetType(const LibraryLoader& loader,
                      const rapidjson::Value& type);

  // Gets a Type object representing the |type|.  |type| is a JSON object with a
  // "subtype" field that represents a scalar type (e.g., "float64", "uint32")
  static Type TypeFromPrimitive(const rapidjson::Value& type);

  // Gets a Type object representing the |type_name|.  |type| is a string that
  // represents a scalar type (e.g., "float64", "uint32").
  static Type ScalarTypeFromName(const std::string& type_name);

  // Gets a Type object representing the |type|.  |type| is a JSON object a
  // field "kind" that states the type.  "kind" is an identifier
  // (e.g.,"foo.bar/Baz").  |loader| is the set of libraries to use to lookup
  // that identifier.
  static Type TypeFromIdentifier(const LibraryLoader& loader,
                                 const rapidjson::Value& type);

  // Returns a reference to a singleton illegal type value.
  static const Type& get_illegal();

  // The size of the given type when it is embedded in another object (struct,
  // array, etc).
  static size_t InlineSizeFromType(const LibraryLoader& loader,
                                   const rapidjson::Value& type);

  // The size of the given type when it is embedded in another object (struct,
  // array, etc).
  static size_t InlineSizeFromIdentifier(const LibraryLoader& loader,
                                         const rapidjson::Value& type);

  Type& operator=(const Type& other) = default;
  Type(const Type& other) = default;

 private:
  Type() {}

  PrintFunction printer_;
  EqualityFunction equals_;
};

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

  Type GetType() const;

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
      : enclosing_library_(library) {
    for (auto& method : value["methods"].GetArray()) {
      interface_methods_.emplace_back(*this, method);
    }
  }

  void AddMethodsToIndex(std::map<Ordinal, const InterfaceMethod*>& index) {
    for (size_t i = 0; i < interface_methods_.size(); i++) {
      const InterfaceMethod* method = &interface_methods_[i];
      Ordinal ordinal = method->get_ordinal();
      index[ordinal] = method;
    }
  }

  const Library& enclosing_library() const { return enclosing_library_; }

 private:
  const Library& enclosing_library_;
  std::vector<InterfaceMethod> interface_methods_;
};

class Enum {
 public:
  Enum(const Library& enclosing_library, const rapidjson::Value& value)
      : enclosing_library_(enclosing_library),
        value_(value),
        type_(Type::ScalarTypeFromName(value_["type"].GetString())) {
    (void)value_;
    (void)enclosing_library_;
  }

  const Type GetType() const { return type_; }

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
    for (auto& member : value_["members"].GetArray()) {
      if (type_.ValueEquals(bytes, length, member["value"]["literal"])) {
        return member["name"].GetString();
      }
    }
    return "(Unknown enum member)";
  }

  uint32_t size() const;

 private:
  const Library& enclosing_library_;
  const rapidjson::Value& value_;
  const Type type_;
};

class StructMember {
 public:
  StructMember(const Struct& enclosing_struct, const rapidjson::Value& value)
      : enclosing_struct_(enclosing_struct), value_(value) {
    // TODO: delete the following line
    (void)value_;
  }

  Type GetType() const;

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

  Type TypeFromIdentifier(bool is_nullable, std::string& identifier) const;

  // The size of the type with name |identifier| when it is inline (e.g.,
  // embedded in an array)
  size_t InlineSizeFromIdentifier(std::string& identifier) const;

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
  bool GetLibraryFromName(std::string& name, const Library** library) const {
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
