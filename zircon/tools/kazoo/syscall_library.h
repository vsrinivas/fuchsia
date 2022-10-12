// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_KAZOO_SYSCALL_LIBRARY_H_
#define ZIRCON_TOOLS_KAZOO_SYSCALL_LIBRARY_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "rapidjson/document.h"
#include "tools/kazoo/macros.h"

class Alias;
class Enum;
class Struct;
class SyscallLibrary;

class TypeBool {};
class TypeChar {};
class TypeInt8 {};
class TypeInt16 {};
class TypeInt32 {};
class TypeInt64 {};
class TypeSizeT {};
class TypeUint16 {};
class TypeUint32 {};
class TypeUint64 {};
class TypeUint8 {};
class TypeUintptrT {};
class TypeVoid {};
class TypeZxBasicAlias;

class TypeAlias;
class TypeEnum;
class TypeHandle;
class TypePointer;
class TypeString {};
class TypeStruct;
class TypeVector;
using TypeData = std::variant<std::monostate, TypeBool, TypeChar, TypeInt8, TypeInt16, TypeInt32,
                              TypeInt64, TypeSizeT, TypeUint16, TypeUint32, TypeUint64, TypeUint8,
                              TypeUintptrT, TypeVoid, TypeZxBasicAlias, TypeAlias, TypeEnum,
                              TypeHandle, TypePointer, TypeString, TypeStruct, TypeVector>;

class Type;

class TypeEnum {
 public:
  explicit TypeEnum(const Enum* enum_data) : enum_(enum_data) {}

  const Enum& enum_data() const { return *enum_; }

 private:
  const Enum* enum_;
};

class TypeHandle {
 public:
  explicit TypeHandle(const std::string& handle_type) : handle_type_(handle_type) {}

  const std::string& handle_type() const { return handle_type_; }

 private:
  std::string handle_type_;
};

class IsDecayedVectorTag {};

class TypePointer {
 public:
  explicit TypePointer(const Type& pointed_to_type)
      : pointed_to_type_(std::make_shared<Type>(pointed_to_type)) {}

  TypePointer(const Type& pointed_to_type, IsDecayedVectorTag)
      : pointed_to_type_(std::make_shared<Type>(pointed_to_type)), was_vector_(true) {}

  const Type& pointed_to_type() const;
  bool was_vector() const { return was_vector_; }

 private:
  std::shared_ptr<Type> pointed_to_type_;

  // This is set to true when the pointer was converted from a vector to a
  // pointer when changing from FIDL type to the target language's type. This
  // indicates that the pointer points the base of an array of
  // pointed-to-types, rather than just pointing at a single one.
  bool was_vector_{false};
};

class TypeAlias {
 public:
  explicit TypeAlias(const Alias* alias_data) : alias_(alias_data) {}

  const Alias& alias_data() const { return *alias_; }

 private:
  const Alias* alias_;
};

class TypeStruct {
 public:
  explicit TypeStruct(const Struct* struct_data) : struct_(struct_data) {}

  const Struct& struct_data() const { return *struct_; }

 private:
  const Struct* struct_;
};

class UseUint32ForVectorSizeTag {};

class TypeVector {
 public:
  explicit TypeVector(const Type& contained_type)
      : contained_type_(std::make_shared<Type>(contained_type)) {}

  explicit TypeVector(const Type& contained_type, UseUint32ForVectorSizeTag)
      : contained_type_(std::make_shared<Type>(contained_type)), uint32_size_(true) {}

  const Type& contained_type() const;
  bool uint32_size() const { return uint32_size_; }

 private:
  std::shared_ptr<Type> contained_type_;
  bool uint32_size_{false};
};

// A FIDL alias pointing to one of the zircon "builtins". e.g. Futex, koid.
// We want to implement special treatment for these types.
class TypeZxBasicAlias {
 public:
  explicit TypeZxBasicAlias(const std::string& name);

  const std::string& name() const { return name_; }
  const std::string& c_name() const { return c_name_; }

 private:
  std::string name_;
  std::string c_name_;
};

inline const Type& TypeVector::contained_type() const { return *contained_type_; }
inline const Type& TypePointer::pointed_to_type() const { return *pointed_to_type_; }

enum class Constness {
  kUnspecified,
  kConst,
  kMutable,
};

enum class Optionality {
  kUnspecified,
  kInputArgument,
  kOutputNonOptional,
  kOutputOptional,
};

class Type {
 public:
  Type() = default;
  explicit Type(TypeData type_data) : type_data_(std::move(type_data)) {}
  Type(TypeData type_data, Constness constness)
      : type_data_(std::move(type_data)), constness_(constness) {}
  Type(TypeData type_data, Constness constness, Optionality optionality)
      : type_data_(std::move(type_data)), constness_(constness), optionality_(optionality) {}
  ~Type() = default;

  const TypeData& type_data() const { return type_data_; }
  void set_type_data(const TypeData& type_data) { type_data_ = type_data; }

  Optionality optionality() const { return optionality_; }
  void set_optionality(Optionality optionality) { optionality_ = optionality; }

  Constness constness() const { return constness_; }
  void set_constness(Constness constness) { constness_ = constness; }

  bool IsChar() const { return std::holds_alternative<TypeChar>(type_data_); }
  bool IsVoid() const { return std::holds_alternative<TypeVoid>(type_data_); }
  bool IsVector() const { return std::holds_alternative<TypeVector>(type_data_); }
  bool IsPointer() const { return std::holds_alternative<TypePointer>(type_data_); }
  bool IsString() const { return std::holds_alternative<TypeString>(type_data_); }
  bool IsStruct() const { return std::holds_alternative<TypeStruct>(type_data_); }
  bool IsHandle() const { return std::holds_alternative<TypeHandle>(type_data_); }
  bool IsSignedInt() const {
    return std::holds_alternative<TypeChar>(type_data_) ||
           std::holds_alternative<TypeInt8>(type_data_) ||
           std::holds_alternative<TypeInt16>(type_data_) ||
           std::holds_alternative<TypeInt32>(type_data_) ||
           std::holds_alternative<TypeInt64>(type_data_);
  }
  bool IsUnsignedInt() const {
    return std::holds_alternative<TypeUint8>(type_data_) ||
           std::holds_alternative<TypeUint16>(type_data_) ||
           std::holds_alternative<TypeUint32>(type_data_) ||
           std::holds_alternative<TypeUint64>(type_data_);
  }

  const TypeVector& DataAsVector() const { return std::get<TypeVector>(type_data_); }
  const TypePointer& DataAsPointer() const { return std::get<TypePointer>(type_data_); }
  const TypeStruct& DataAsStruct() const { return std::get<TypeStruct>(type_data_); }

  bool IsSimpleType() const { return !IsVector() && !IsString() && !IsStruct(); }

 private:
  TypeData type_data_;
  Constness constness_{Constness::kUnspecified};
  Optionality optionality_{Optionality::kUnspecified};
};

class Alias {
 public:
  Alias() = default;
  ~Alias() = default;

  const std::string& id() const { return id_; }
  const std::string& original_name() const { return original_name_; }
  const std::string& base_name() const { return base_name_; }
  const std::string& partial_type_ctor() const { return partial_type_ctor_; }
  const std::vector<std::string>& description() const { return description_; }

 private:
  friend class SyscallLibraryLoader;

  std::string id_;                 // "zx/MyAlias"
  std::string original_name_;      // "MyAlias"
  std::string base_name_;          // "my_alias"
  std::string partial_type_ctor_;  // "uint64"
  std::vector<std::string> description_;

  DISALLOW_COPY_AND_ASSIGN(Alias);
};

class StructMember {
 public:
  StructMember() = default;
  StructMember(const std::string& name, const Type& type,
               const std::map<std::string, std::string>& attributes)
      : name_(name), type_(type), attributes_(attributes) {}
  ~StructMember() = default;

  const std::string& name() const { return name_; }

  const Type& type() const { return type_; }
  void set_type(const Type& type) { type_ = type; }

  const std::map<std::string, std::string>& attributes() const { return attributes_; }

 private:
  friend class SyscallLibraryLoader;

  std::string name_;
  Type type_;
  std::map<std::string, std::string> attributes_;
};

class Struct {
 public:
  Struct() = default;
  ~Struct() = default;

  const std::string& id() const { return id_; }
  const std::string& original_name() const { return original_name_; }
  const std::string& base_name() const { return base_name_; }
  const std::vector<StructMember>& members() const { return members_; }

 private:
  friend class SyscallLibraryLoader;

  std::string id_;             // "zx/HandleInfo"
  std::string original_name_;  // "HandleInfo"
  std::string base_name_;      // "handle_info"
  std::vector<StructMember> members_;

  DISALLOW_COPY_AND_ASSIGN(Struct);
};

class Syscall {
 public:
  Syscall() = default;
  ~Syscall() = default;

  const std::map<std::string, std::string>& attributes() const { return attributes_; }

  const std::string& id() const { return id_; }
  const std::string& name() const { return name_; }
  const std::string& category() const { return category_; }
  const std::string& snake_name() const { return snake_name_; }
  bool is_noreturn() const { return is_noreturn_; }
  const Struct& request() const { return request_; }
  const Struct& response() const { return response_; }

  bool HasAttribute(const char* attrib_name) const;
  std::string GetAttribute(const char* attrib_name) const;

  const Type& kernel_return_type() const { return kernel_return_type_; }
  const std::vector<StructMember>& kernel_arguments() const { return kernel_arguments_; }
  size_t num_kernel_args() const { return kernel_arguments_.size(); }

 private:
  friend class SyscallLibraryLoader;
  bool MapRequestResponseToKernelAbi();
  bool HandleArgReorder();

  std::string id_;          // "zx/Object"
  std::string name_;        // "GetInfo"
  std::string category_;    // "object"
  std::string snake_name_;  // "object_get_info"
  bool is_noreturn_ = false;
  std::map<std::string, std::string> attributes_;
  Struct request_;
  Struct response_;

  // request_ and response_ mapped to C/Kernel-style call style.
  Type kernel_return_type_;
  std::vector<StructMember> kernel_arguments_;

  DISALLOW_COPY_AND_ASSIGN(Syscall);
};

enum class Required { kNo, kYes };

class TableMember {
 public:
  TableMember() = default;
  TableMember(const std::string& name, const Type& type,
              const std::vector<std::string>& description, Required required)
      : name_(name), type_(type), description_(description), required_(required) {}
  ~TableMember() = default;

  const std::string& name() const { return name_; }
  const Type& type() const { return type_; }
  const std::vector<std::string>& description() const { return description_; }
  Required required() const { return required_; }

 private:
  friend class SyscallLibraryLoader;

  std::string name_;
  Type type_;
  std::vector<std::string> description_;
  Required required_;
};

class Table {
 public:
  Table() = default;
  ~Table() = default;

  const std::string& id() const { return id_; }
  const std::string& original_name() const { return original_name_; }
  const std::string& base_name() const { return base_name_; }
  const std::vector<std::string>& description() const { return description_; }
  const std::vector<TableMember>& members() const { return members_; }

 private:
  friend class SyscallLibraryLoader;

  std::string id_;             // "zx/HandleInfo"
  std::string original_name_;  // "HandleInfo"
  std::string base_name_;      // "handle_info"
  std::vector<std::string> description_;
  std::vector<TableMember> members_;

  DISALLOW_COPY_AND_ASSIGN(Table);
};

struct EnumMember {
  uint64_t value;
  std::vector<std::string> description;
};

class Enum {
 public:
  Enum() = default;
  ~Enum() = default;

  const std::string& id() const { return id_; }                        // "zx/ProfileInfoType"
  const std::string& original_name() const { return original_name_; }  // "ProfileInfoType"
  const std::string& base_name() const { return base_name_; }          // "profile_info_type"
  const std::vector<std::string>& description() const { return description_; }

  void AddMember(const std::string& member_name, EnumMember member);
  bool HasMember(const std::string& member_name) const;
  const EnumMember& ValueForMember(const std::string& member_name) const;

  const std::vector<std::string>& members() const { return insertion_order_; }
  const Type& underlying_type() const { return underlying_type_; }

 private:
  friend class SyscallLibraryLoader;

  std::string id_;
  std::string original_name_;
  std::string base_name_;
  std::vector<std::string> description_;
  Type underlying_type_;                       // uint32_t etc.
  std::map<std::string, EnumMember> members_;  // Maps enumeration name to value (kWhatever = 12).
  std::vector<std::string> insertion_order_;
};

class SyscallLibrary {
 public:
  SyscallLibrary() = default;
  ~SyscallLibrary() = default;

  const std::string& name() const { return name_; }  // "zx"
  const std::vector<std::unique_ptr<Enum>>& bits() const { return bits_; }
  const std::vector<std::unique_ptr<Enum>>& enums() const { return enums_; }
  const std::vector<std::unique_ptr<Syscall>>& syscalls() const { return syscalls_; }
  const std::vector<std::unique_ptr<Alias>>& aliases() const { return aliases_; }
  const std::vector<std::unique_ptr<Table>>& tables() const { return tables_; }

  Type TypeFromIdentifier(const std::string& id) const;
  Type TypeFromName(const std::string& name) const;

  void FilterSyscalls(const std::set<std::string>& attributes_to_exclude);

 private:
  friend class SyscallLibraryLoader;

  std::string name_;
  std::vector<std::unique_ptr<Enum>> bits_;
  std::vector<std::unique_ptr<Enum>> enums_;
  std::vector<std::unique_ptr<Struct>> structs_;
  std::vector<std::unique_ptr<Syscall>> syscalls_;
  std::vector<std::unique_ptr<Alias>> aliases_;
  std::vector<std::unique_ptr<Table>> tables_;

  DISALLOW_COPY_AND_ASSIGN(SyscallLibrary);
};

class SyscallLibraryLoader {
 public:
  // Loads a JSON representation of syscalls into a SyscallLibrary structure.
  // Returns true on success, or false with a message logged.
  static bool FromJson(const std::string& json_ir, SyscallLibrary* library);

 private:
  static bool ExtractPayload(Struct& payload, const std::string& type_name,
                             const rapidjson::Document& document, SyscallLibrary* library);
  static bool LoadBits(const rapidjson::Document& document, SyscallLibrary* library);
  static bool LoadEnums(const rapidjson::Document& document, SyscallLibrary* library);
  static bool LoadProtocols(const rapidjson::Document& document, SyscallLibrary* library);
  static bool LoadAliases(const rapidjson::Document& document, SyscallLibrary* library);
  static bool LoadStructs(const rapidjson::Document& document, SyscallLibrary* library);
  static bool LoadTables(const rapidjson::Document& document, SyscallLibrary* library);

  static std::unique_ptr<Enum> ConvertBitsOrEnumMember(const rapidjson::Value& json);
};

#endif  // ZIRCON_TOOLS_KAZOO_SYSCALL_LIBRARY_H_
