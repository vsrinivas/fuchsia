// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_LIBRARY_LOADER_H_
#define TOOLS_FIDLCAT_LIB_LIBRARY_LOADER_H_

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

class Type;
class Library;
class LibraryLoader;
class Interface;
class InterfaceMethod;
class Enum;
class Struct;
class Union;

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
  friend class Interface;
  InterfaceMethod(const Interface& interface, const rapidjson::Value& value);
  InterfaceMethod(InterfaceMethod&& other);

  Ordinal get_ordinal() const {
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

  Interface(Interface&& other)
      : value_(other.value_), enclosing_library_(other.enclosing_library_) {
    for (auto& method : other.interface_methods_) {
      interface_methods_.emplace_back(*this, method.value_);
    }
  }

  Interface(const Interface& other) = delete;
  Interface& operator=(const Interface&) = delete;

  std::string name() const { return value_["name"].GetString(); }

  void AddMethodsToIndex(std::map<Ordinal, const InterfaceMethod*>& index) {
    for (size_t i = 0; i < interface_methods_.size(); i++) {
      const InterfaceMethod* method = &interface_methods_[i];
      Ordinal ordinal = method->get_ordinal();
      index[ordinal] = method;
    }
  }

  // Sets *|method| to the fully qualified |name|'s InterfaceMethod
  bool GetMethodByFullName(const std::string& name,
                           const InterfaceMethod** method) const;

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

  const std::unique_ptr<Type> GetType() const;

  // Gets the name of the enum member corresponding to the value pointed to by
  // |bytes| of length |length|.  For example, if we had the following
  // definition:
  // enum i16_enum : int16 {
  //   x = -23;
  // };
  // and you pass |bytes| a 2-byte representation of -23, and |length| 2, this
  // function will return "x".  Returns "(Unknown enum member)" if it can't find
  // the member.
  std::string GetNameFromBytes(const uint8_t* bytes, size_t length) const;

  uint32_t size() const;

 private:
  const Library& enclosing_library_;
  const rapidjson::Value& value_;
};

// TODO: Consider whether this is duplicative of Struct / Table / XUnion member.
class UnionMember {
 public:
  UnionMember(const Union& enclosing_union, const rapidjson::Value& value)
      : enclosing_union_(enclosing_union), value_(value) {}

  virtual std::unique_ptr<Type> GetType() const;

  virtual uint64_t size() const {
    return std::strtoll(value_["size"].GetString(), nullptr, 10);
  }

  virtual uint64_t offset() const {
    return std::strtoll(value_["offset"].GetString(), nullptr, 10);
  }

  virtual std::string name() const { return value_["name"].GetString(); }

  virtual const Union& enclosing_union() const { return enclosing_union_; }

  virtual ~UnionMember() {}

 private:
  const Union& enclosing_union_;
  const rapidjson::Value& value_;
};

class Union {
  friend class Library;

 public:
  Union(const Library& enclosing_library, const rapidjson::Value& value)
      : enclosing_library_(enclosing_library),
        value_(value),
        illegal_(nullptr) {
    auto member_arr = value["members"].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
      members_.emplace_back(*this, member);
    }
  }

  Union(const Union& other) : Union(other.enclosing_library_, other.value_) {}

  const Library& enclosing_library() const { return enclosing_library_; }

  const std::vector<UnionMember>& members() const { return members_; }

  const UnionMember& MemberWithTag(uint32_t tag) const;

  uint64_t alignment() const {
    return std::strtoll(value_["alignment"].GetString(), nullptr, 10);
  }

  uint32_t size() const {
    return std::strtoll(value_["size"].GetString(), nullptr, 10);
  }

 private:
  const rapidjson::Value& schema() const { return value_; }

  const UnionMember& get_illegal_member() const;

  const Library& enclosing_library_;
  const rapidjson::Value& value_;
  std::vector<UnionMember> members_;
  mutable std::unique_ptr<UnionMember> illegal_;
};

class StructMember {
 public:
  StructMember(const Struct& enclosing_struct, const rapidjson::Value& value)
      : enclosing_struct_(enclosing_struct), value_(value) {}

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
    auto member_arr = value["members"].GetArray();
    members_.reserve(member_arr.Size());
    for (auto& member : member_arr) {
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
    auto interfaces_array =
        backing_document_["interface_declarations"].GetArray();
    interfaces_.reserve(interfaces_array.Size());

    for (auto& decl : interfaces_array) {
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
    for (auto& str : backing_document_["union_declarations"].GetArray()) {
      unions_.emplace(std::piecewise_construct,
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

  // Set *ptr to the Interface called |name|
  bool GetInterfaceByName(const std::string& name, const Interface** ptr) const;

  Library& operator=(const Library&) = delete;
  Library(const Library&) = delete;

 private:
  const LibraryLoader& enclosing_loader_;
  rapidjson::Document backing_document_;

  std::vector<Interface> interfaces_;
  std::map<std::string, Enum> enums_;
  std::map<std::string, Struct> structs_;
  std::map<std::string, Union> unions_;
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

#endif  // TOOLS_FIDLCAT_LIB_LIBRARY_LOADER_H_
