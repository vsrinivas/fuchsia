// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_LIBRARY_LOADER_H_
#define SRC_LIB_FIDL_CODEC_LIBRARY_LOADER_H_

#include <iostream>
#include <map>
#include <optional>
#include <vector>

#include "rapidjson/document.h"

// This file contains a programmatic representation of a FIDL schema.  A
// LibraryLoader loads a set of Libraries.  The libraries contain structs,
// enums, interfaces, and so on.  Each element has the logic necessary to take
// wire-encoded bits of that type, and transform it to a representation of that
// type.

// A LibraryLoader object can be used to fetch a particular library or interface
// method, which can then be used for debug purposes.

// An example of building a LibraryLoader can be found in
// library_loader_test.cc:LoadSimple. Callers can then do something like the
// following, if they have a fidl::Message:
//
// fidl_message_header_t header = message.header();
// const std::vector<const InterfaceMethod*>* methods = loader_->GetByOrdinal(header.ordinal);
// rapidjson::Document actual;
// fidl_codec::RequestToJSON(methods->at(0), message, actual);
//
// |actual| will then contain the contents of the message in JSON
// (human-readable) format.
//
// These libraries are currently thread-unsafe.

namespace fidl_codec {

constexpr int kDecimalBase = 10;

typedef uint32_t Ordinal32;
typedef uint64_t Ordinal64;

struct LibraryReadError {
  enum ErrorValue {
    kOk,
    kIoError,
    kParseError,
  };
  ErrorValue value;
  rapidjson::ParseResult parse_result;
};

class Interface;
class InterfaceMethod;
class Library;
class LibraryLoader;
class MessageDecoder;
class Struct;
class StructValue;
class Table;
class Type;
class TypeVisitor;
class Union;

class EnumOrBitsMember {
  friend class Enum;
  friend class Bits;

 public:
  EnumOrBitsMember(const std::string_view& name, uint64_t absolute_value, bool negative)
      : name_(name), absolute_value_(absolute_value), negative_(negative) {}

  const std::string& name() const { return name_; }
  uint64_t absolute_value() const { return absolute_value_; }
  bool negative() const { return negative_; }

 private:
  const std::string name_;
  const uint64_t absolute_value_;
  const bool negative_;
};

class EnumOrBits {
 public:
  friend class Library;

  ~EnumOrBits();

  const std::string& name() const { return name_; }
  uint64_t size() const { return size_; }
  const Type* type() const { return type_.get(); }

  // Get a list of Enum members.
  const std::vector<EnumOrBitsMember>& members() const { return members_; }

 protected:
  EnumOrBits(const rapidjson::Value* json_definition);

  // Decode all the values from the JSON definition.
  void DecodeTypes(bool is_scalar, const std::string& supertype_name, Library* enclosing_library);

 private:
  const rapidjson::Value* json_definition_;
  std::string name_;
  uint64_t size_;
  std::unique_ptr<Type> type_;
  std::vector<EnumOrBitsMember> members_;
};

class Enum : public EnumOrBits {
 public:
  friend class Library;

  ~Enum();

  // Gets the name of the enum member corresponding to the value pointed to by
  // |bytes| of length |length|.  For example, if we had the following
  // definition:
  // enum i16_enum : int16 {
  //   x = -23;
  // };
  // and you pass |bytes| a 2-byte representation of -23, and |length| 2, this
  // function will return "x".  Returns "(Unknown enum member)" if it can't find
  // the member.
  std::string GetName(uint64_t absolute_value, bool negative) const;

 private:
  Enum(const rapidjson::Value* json_definition) : EnumOrBits(json_definition) {}

  void DecodeTypes(Library* enclosing_library) {
    return EnumOrBits::DecodeTypes(true, "enum", enclosing_library);
  }
};

class Bits : public EnumOrBits {
 public:
  friend class Library;

  ~Bits();

  std::string GetName(uint64_t absolute_value, bool negative) const;

 private:
  Bits(const rapidjson::Value* json_definition) : EnumOrBits(json_definition) {}

  void DecodeTypes(Library* enclosing_library) {
    return EnumOrBits::DecodeTypes(false, "bits", enclosing_library);
  }
};

class UnionMember {
 public:
  UnionMember(const Union& union_definition, Library* enclosing_library,
              const rapidjson::Value* json_definition, bool for_xunion);
  ~UnionMember();

  const Union& union_definition() const { return union_definition_; }
  bool reserved() const { return reserved_; }
  const std::string& name() const { return name_; }
  uint64_t offset() const { return offset_; }
  uint64_t size() const { return size_; }
  Ordinal32 ordinal() const { return ordinal_; }
  const Type* type() const { return type_.get(); }

 private:
  const Union& union_definition_;
  const bool reserved_;
  const std::string name_;
  const uint64_t offset_;
  const uint64_t size_;
  const Ordinal32 ordinal_;
  std::unique_ptr<Type> type_;
};

class Union {
 public:
  friend class Library;

  Library* enclosing_library() const { return enclosing_library_; }
  const std::string& name() const { return name_; }
  uint64_t alignment() const { return alignment_; }
  uint32_t size() const { return size_; }
  const std::vector<std::unique_ptr<UnionMember>>& members() const { return members_; }

  const UnionMember* MemberWithTag(uint32_t tag) const;

  const UnionMember* MemberWithOrdinal(Ordinal32 ordinal) const;

 private:
  Union(Library* enclosing_library, const rapidjson::Value* json_definition);

  // Decode all the values from the JSON definition.
  void DecodeTypes(bool for_xunion);
  void DecodeUnionTypes() { DecodeTypes(/*for_xunion=*/false); }
  void DecodeXunionTypes() { DecodeTypes(/*for_xunion=*/true); }

  Library* enclosing_library_;
  const rapidjson::Value* json_definition_;
  std::string name_;
  uint64_t alignment_;
  uint64_t size_;
  std::vector<std::unique_ptr<UnionMember>> members_;
};

class StructMember {
 public:
  StructMember(Library* enclosing_library, const rapidjson::Value* json_definition);
  ~StructMember();

  const std::string& name() const { return name_; }
  uint64_t v0_offset() const { return v0_offset_; }
  uint64_t v1_offset() const { return v1_offset_; }
  const Type* type() const { return type_.get(); }

  uint64_t Offset(MessageDecoder* decoder) const;

 private:
  const std::string name_;
  const uint64_t size_;
  uint64_t v0_offset_;
  uint64_t v1_offset_;
  std::unique_ptr<Type> type_;
};

class Struct {
 public:
  friend class Library;
  friend class InterfaceMethod;

  Library* enclosing_library() const { return enclosing_library_; }
  const std::string& name() const { return name_; }
  uint32_t v0_size() const { return v0_size_; }
  uint32_t v1_size() const { return v1_size_; }
  const std::vector<std::unique_ptr<StructMember>>& members() const { return members_; }

  uint32_t Size(bool unions_are_xunions) const;

  // Wrap this Struct in a non-nullable type and use the given visitor on it.
  void VisitAsType(TypeVisitor* visitor) const;

  // Get a string representation for this struct.
  std::string ToString(bool expand = false) const;

 private:
  Struct(Library* enclosing_library, const rapidjson::Value* json_definition);

  // Decode all the values from the JSON definition if the object represents a
  // structure.
  void DecodeStructTypes();

  // Decode all the values from the JSON definition if the object represents a
  // request message.
  void DecodeRequestTypes();

  // Decode all the values from the JSON definition if the object represents a
  // response message.
  void DecodeResponseTypes();

  // Decode all the values from the JSON definition.
  void DecodeTypes(std::string_view container_name, const char* size_name, const char* member_name,
                   const char* v0_name, const char* v1_name);

  Library* enclosing_library_;
  const rapidjson::Value* json_definition_;
  std::string name_;
  uint32_t v0_size_ = 0;
  uint32_t v1_size_ = 0;
  std::vector<std::unique_ptr<StructMember>> members_;
};

class TableMember {
 public:
  TableMember(Library* enclosing_library, const rapidjson::Value* json_definition);
  ~TableMember();

  bool reserved() const { return reserved_; }
  const std::string& name() const { return name_; }
  Ordinal32 ordinal() const { return ordinal_; }
  uint64_t size() const { return size_; }
  const Type* type() const { return type_.get(); }

 private:
  const bool reserved_;
  const std::string name_;
  const Ordinal32 ordinal_;
  const uint64_t size_;
  std::unique_ptr<Type> type_;
};

class Table {
 public:
  friend class Library;

  Table(Library* enclosing_library, const rapidjson::Value* json_definition);
  ~Table();

  Library* enclosing_library() const { return enclosing_library_; }
  const std::string& name() const { return name_; }
  uint32_t size() const { return size_; }
  const std::vector<std::unique_ptr<TableMember>>& members() const { return members_; }

  const TableMember* GetMember(uint64_t ordinal) const {
    if (ordinal >= members_.size()) {
      return nullptr;
    }
    return members_[ordinal].get();
  }
  const TableMember* GetMember(std::string_view name) const {
    for (const auto& member : members_) {
      if (member->name() == name) {
        return member.get();
      }
    }
    return nullptr;
  }

 private:
  // Decode all the values from the JSON definition.
  void DecodeTypes();

  Library* enclosing_library_;
  const rapidjson::Value* json_definition_;
  std::string name_;
  uint64_t size_;
  std::vector<std::unique_ptr<TableMember>> members_;
};

class InterfaceMethod {
 public:
  friend class Interface;

  const Interface& enclosing_interface() const { return enclosing_interface_; }
  const std::string& name() const { return name_; }
  Ordinal64 ordinal() const { return ordinal_; }
  Ordinal64 old_ordinal() const { return old_ordinal_; }
  bool is_composed() const { return is_composed_; }
  Struct* request() const {
    if (request_ != nullptr) {
      request_->DecodeRequestTypes();
    }
    return request_.get();
  }
  Struct* response() const {
    if (response_ != nullptr) {
      response_->DecodeResponseTypes();
    }
    return response_.get();
  }

  std::string fully_qualified_name() const;

  InterfaceMethod(const InterfaceMethod& other) = delete;
  InterfaceMethod& operator=(const InterfaceMethod&) = delete;

 private:
  InterfaceMethod(const Interface& interface, const rapidjson::Value* json_definition);

  const Interface& enclosing_interface_;
  const std::string name_;
  const Ordinal64 ordinal_;
  const Ordinal64 old_ordinal_;
  const bool is_composed_;
  std::unique_ptr<Struct> request_;
  std::unique_ptr<Struct> response_;
};

class Interface {
 public:
  friend class Library;

  Interface(const Interface& other) = delete;
  Interface& operator=(const Interface&) = delete;

  Library* enclosing_library() const { return enclosing_library_; }
  const std::string& name() const { return name_; }

  void AddMethodsToIndex(LibraryLoader* library_loader);

  // Sets *|method| to the fully qualified |name|'s InterfaceMethod
  bool GetMethodByFullName(const std::string& name, const InterfaceMethod** method) const;

  const std::vector<std::unique_ptr<InterfaceMethod>>& methods() const {
    return interface_methods_;
  }

 private:
  Interface(Library* enclosing_library, const rapidjson::Value& json_definition)
      : enclosing_library_(enclosing_library), name_(json_definition["name"].GetString()) {
    for (auto& method : json_definition["methods"].GetArray()) {
      interface_methods_.emplace_back(new InterfaceMethod(*this, &method));
    }
  }

  Library* enclosing_library_;
  std::string name_;
  std::vector<std::unique_ptr<InterfaceMethod>> interface_methods_;
};

class Library {
 public:
  friend class LibraryLoader;

  LibraryLoader* enclosing_loader() const { return enclosing_loader_; }
  const std::string& name() const { return name_; }
  const std::vector<std::unique_ptr<Interface>>& interfaces() const { return interfaces_; }

  // Decode all the content of this FIDL file.
  bool DecodeAll();

  std::unique_ptr<Type> TypeFromIdentifier(bool is_nullable, std::string& identifier,
                                           size_t inline_size);

  // The size of the type with name |identifier| when it is inline (e.g.,
  // embedded in an array)
  size_t InlineSizeFromIdentifier(std::string& identifier) const;

  // Set *ptr to the Interface called |name|
  bool GetInterfaceByName(const std::string& name, const Interface** ptr) const;

  // Extract a boolean field from a JSON value.
  bool ExtractBool(const rapidjson::Value* json_definition, std::string_view container_type,
                   std::string_view container_name, const char* field_name);
  // Extract a string field from a JSON value.
  std::string ExtractString(const rapidjson::Value* json_definition,
                            std::string_view container_type, std::string_view container_name,
                            const char* field_name);
  // Extract a uint64_t field from a JSON value.
  uint64_t ExtractUint64(const rapidjson::Value* json_definition, std::string_view container_type,
                         std::string_view container_name, const char* field_name);
  // Extract a uint32_t field from a JSON value.
  uint32_t ExtractUint32(const rapidjson::Value* json_definition, std::string_view container_type,
                         std::string_view container_name, const char* field_name);
  // Extract a scalar type from a JSON value.
  std::unique_ptr<Type> ExtractScalarType(const rapidjson::Value* json_definition,
                                          std::string_view container_type,
                                          std::string_view container_name, const char* field_name,
                                          uint64_t size);
  // Extract a type from a JSON value.
  std::unique_ptr<Type> ExtractType(const rapidjson::Value* json_definition,
                                    std::string_view container_type,
                                    std::string_view container_name, const char* field_name,
                                    uint64_t size);
  // Display an error when a field is not found.
  void FieldNotFound(std::string_view container_type, std::string_view container_name,
                     const char* field_name);

  const Table* GetTable(const std::string& table_name) const {
    auto result = tables_.find(table_name);
    if (result == tables_.end()) {
      return nullptr;
    }
    return result->second.get();
  }

  Library& operator=(const Library&) = delete;
  Library(const Library&) = delete;
  ~Library();

 private:
  Library(LibraryLoader* enclosing_loader, rapidjson::Document& json_definition);

  // Decode all the values from the JSON definition.
  void DecodeTypes();

  LibraryLoader* enclosing_loader_;
  rapidjson::Document json_definition_;
  bool decoded_ = false;
  bool has_errors_ = false;
  std::string name_;
  std::vector<std::unique_ptr<Interface>> interfaces_;
  std::map<std::string, std::unique_ptr<Enum>> enums_;
  std::map<std::string, std::unique_ptr<Bits>> bits_;
  std::map<std::string, std::unique_ptr<Union>> unions_;
  std::map<std::string, std::unique_ptr<Union>> xunions_;
  std::map<std::string, std::unique_ptr<Struct>> structs_;
  std::map<std::string, std::unique_ptr<Table>> tables_;
};

// An indexed collection of libraries.
// WARNING: All references on Enum, Struct, Table, ... and all references on
//          types and fields must be destroyed before this class (LibraryLoader
//          should be one of the last objects we destroy).
class LibraryLoader {
 public:
  friend class Library;
  // Creates a LibraryLoader populated by the given library streams.
  LibraryLoader(std::vector<std::unique_ptr<std::istream>>* library_streams, LibraryReadError* err);

  // Creates a LibraryLoader with no libraries
  LibraryLoader() = default;

  LibraryLoader& operator=(const LibraryLoader&) = delete;
  LibraryLoader(const LibraryLoader&) = delete;

  // Add the libraries for all the streams.
  bool AddAll(std::vector<std::unique_ptr<std::istream>>* library_streams, LibraryReadError* err);

  // Decode all the FIDL files.
  bool DecodeAll();

  // Adds a single library (read from library_stream) to this Loader. Sets err as appropriate.
  void Add(std::unique_ptr<std::istream>* library_stream, LibraryReadError* err);

  // Adds a method ordinal to the ordinal map.
  void AddMethod(const InterfaceMethod* method);

  // Returns a pointer to a set of methods that have this ordinal.  There may be
  // more than one if the method was composed into multiple protocols.  For
  // convenience, the methods that are not composed are at the front of the
  // vector.  Returns |nullptr| if there is no such method.  The returned
  // pointer continues to be owned by the LibraryLoader, and should not be
  // deleted.
  const std::vector<const InterfaceMethod*>* GetByOrdinal(Ordinal64 ordinal) {
    auto m = ordinal_map_.find(ordinal);
    if (m != ordinal_map_.end()) {
      return m->second.get();
    }
    return nullptr;
  }

  // If the library with name |name| is present in this loader, returns the
  // library. Otherwise, returns null.
  // |name| is of the format "a.b.c"
  Library* GetLibraryFromName(const std::string& name) {
    auto l = representations_.find(name);
    if (l != representations_.end()) {
      Library* library = l->second.get();
      library->DecodeTypes();
      return library;
    }
    return nullptr;
  }

 private:
  void Add(std::string& ir, LibraryReadError* err) {
    rapidjson::Document document;
    err->parse_result = document.Parse<rapidjson::kParseNumbersAsStringsFlag>(ir.c_str());
    // TODO: This would be a good place to validate that the resulting JSON
    // matches the schema in zircon/tools/fidl/schema.json.  If there are
    // errors, we will currently get mysterious crashes.
    if (document.HasParseError()) {
      err->value = LibraryReadError::kParseError;
      return;
    }
    std::string library_name = document["name"].GetString();
    if (representations_.find(library_name) == representations_.end()) {
      representations_.emplace(std::piecewise_construct, std::forward_as_tuple(library_name),
                               std::forward_as_tuple(new Library(this, document)));
    }
  }

  void Delete(const Library* library) {
    // The only way to delete a library is to remove it from representations_, so we don't need to
    // do that explicitly.  However...
    for (const auto& iface : library->interfaces()) {
      for (const auto& method : iface->methods()) {
        ordinal_map_.erase(method->ordinal());
        ordinal_map_.erase(method->old_ordinal());
      }
    }
  }

  // Because Delete() above is run whenever a Library is destructed, we want ordinal_map_ to be
  // intact when a Library is destructed.  Therefore, ordinal_map_ has to come first.
  std::map<Ordinal64, std::unique_ptr<std::vector<const InterfaceMethod*>>> ordinal_map_;
  std::map<std::string, std::unique_ptr<Library>> representations_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_LIBRARY_LOADER_H_
