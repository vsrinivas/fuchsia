// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FIDLCAT_LIB_LIBRARY_LOADER_H_
#define GARNET_BIN_FIDLCAT_LIB_LIBRARY_LOADER_H_

#include <lib/fidl/cpp/message.h>

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
  rapidjson::ParseErrorCode error_description;
};

class Library;
class LibraryLoader;
class Interface;
class InterfaceMethod;
class Enum;
class Struct;

// Takes a series of bytes pointed to by |bytes| and of length |length|, and
// sets |value| to their representation for a particular type.  Uses |allocator|
// to allocate in |value|.  Returns the length of data read.
typedef std::function<size_t(const uint8_t* bytes, size_t length,
                             rapidjson::Value& value,
                             rapidjson::Document::AllocatorType& allocator)>
    PrintFunction;

// Takes a series of bytes pointed to by |bytes| and of length |length|, and
// returns whether that is equal to the Value represented by |value| according
// to some type.
typedef std::function<bool(const uint8_t* bytes, size_t length,
                           const rapidjson::Value& value)>
    EqualityFunction;

// A FIDL type.  Provides methods for generating instances of this type.
class Type {
  friend class InterfaceMethodParameter;
  friend class Library;

 public:
  // To avoid inheritance, a type takes two functions
  Type(PrintFunction printer, EqualityFunction equals)
      : printer_(printer), equals_(equals) {}

  // Takes a series of bytes pointed to by |bytes| and of length |length|, and
  // sets |value| to their representation given this type.  Uses |allocator| to
  // allocate in |value|
  size_t MakeValue(const uint8_t* bytes, size_t length, rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
    return printer_(bytes, length, value, allocator);
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

  // Gets a Type object representing the |type_anme|.  |type| is a string that
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

  Type TypeFromIdentifier(std::string& identifier) const;

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
    document.Parse<rapidjson::kParseNumbersAsStringsFlag>(ir.c_str());
    // TODO: This would be a good place to validate that the resulting JSON
    // matches the schema in zircon/system/host/fidl/schema.json.  If there are
    // errors, we will currently get mysterious crashes.
    if (document.HasParseError()) {
      err->value = LibraryReadError::kParseError;
      err->error_description = document.GetParseError();
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
