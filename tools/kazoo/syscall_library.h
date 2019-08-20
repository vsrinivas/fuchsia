// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_KAZOO_JSON_TO_LIBRARY_H_
#define TOOLS_KAZOO_JSON_TO_LIBRARY_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "rapidjson/document.h"
#include "src/lib/fxl/macros.h"

class Struct;
class SyscallLibrary;

class TypeBool {};
class TypeChar {};
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

class TypeHandle;
class TypePointer;
class TypeString {};
class TypeStruct;
class TypeVector;
using TypeData =
    std::variant<std::monostate, TypeBool, TypeChar, TypeInt32, TypeInt64, TypeSizeT, TypeUint16,
                 TypeUint32, TypeUint64, TypeUint8, TypeUintptrT, TypeVoid, TypeZxBasicAlias,
                 TypeHandle, TypePointer, TypeString, TypeStruct, TypeVector>;

class Type;

class TypeHandle {
 public:
  explicit TypeHandle(const std::string& handle_type) : handle_type_(handle_type) {}

  const std::string& handle_type() const { return handle_type_; }

 private:
  std::string handle_type_;
};

class TypePointer {
 public:
  explicit TypePointer(const Type& pointed_to_type)
      : pointed_to_type_(std::make_shared<Type>(pointed_to_type)) {}

  const Type& pointed_to_type() const;

 private:
  std::shared_ptr<Type> pointed_to_type_;
};

class TypeStruct {
 public:
  explicit TypeStruct(const Struct* struct_data) : struct_(struct_data) {}

  const Struct& struct_data() const { return *struct_; }

 private:
  const Struct* struct_;
};

class UseUint32ForVectorSizeTag{};

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

class TypeZxBasicAlias {
 public:
  explicit TypeZxBasicAlias(const std::string& name) : name_("zx_" + name + "_t") {}

  const std::string& name() const { return name_; }

 private:
  std::string name_;
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

  const TypeVector& DataAsVector() const { return std::get<TypeVector>(type_data_); }

  bool IsSimpleType() const { return !IsVector() && !IsString() && !IsStruct(); }

 private:
  TypeData type_data_;
  Constness constness_{Constness::kUnspecified};
  Optionality optionality_{Optionality::kUnspecified};
};

class StructMember {
 public:
  StructMember() = default;
  StructMember(const std::string& name, const Type& type) : name_(name), type_(type) {}
  ~StructMember() = default;

  const std::string& name() const { return name_; }

  const Type& type() const { return type_; }
  void set_type(const Type& type) { type_ = type; }

 private:
  friend class SyscallLibraryLoader;

  std::string name_;
  Type type_;
};

class Struct {
 public:
  Struct() = default;
  ~Struct() = default;

  const std::string& id() const { return id_; }
  const std::string& original_name() const { return original_name_; }
  const std::string& name() const { return name_; }
  const std::vector<StructMember>& members() const { return members_; }

 private:
  friend class SyscallLibraryLoader;

  std::string id_;                  // "zx/HandleInfo"
  std::string original_name_;       // "HandleInfo"
  std::string name_;                // "zx_handle_info_t"
  std::vector<StructMember> members_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Struct);
};

class Syscall {
 public:
  Syscall() = default;
  ~Syscall() = default;

  const std::string& short_description() const { return short_description_; }
  const std::map<std::string, std::string>& attributes() const { return attributes_; }

  const std::string& id() const { return id_; }
  const std::string& original_name() const { return original_name_; }
  const std::string& category() const { return category_; }
  const std::string& name() const { return name_; }
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

  std::string id_;                  // "zx/Object"
  std::string original_name_;       // "GetInfo"
  std::string category_;            // "object"
  std::string name_;                // "object_get_info"
  std::string short_description_;
  bool is_noreturn_ = false;
  std::map<std::string, std::string> attributes_;
  Struct request_;
  Struct response_;

  // request_ and response_ mapped to C/Kernel-style call style.
  Type kernel_return_type_;
  std::vector<StructMember> kernel_arguments_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Syscall);
};

class SyscallLibrary {
 public:
  SyscallLibrary() = default;
  ~SyscallLibrary() = default;

  const std::string& name() const { return name_; }  // "zx"
  const std::vector<std::unique_ptr<Syscall>>& syscalls() const { return syscalls_; }

  Type TypeFromIdentifier(const std::string& id) const;

 private:
  friend class SyscallLibraryLoader;

  std::string name_;
  std::vector<std::unique_ptr<Struct>> structs_;
  std::vector<std::unique_ptr<Syscall>> syscalls_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyscallLibrary);
};

class SyscallLibraryLoader {
 public:
  // Loads a JSON representation of syscalls into a SyscallLibrary structure.
  // |match_original_order| can be set to true to make the syscalls be ordered
  // in the same order as syscalls.abigen is today.
  // Returns true on success, or false with a message logged.
  static bool FromJson(const std::string& json_ir, SyscallLibrary* library,
                       bool match_original_order = false);

 private:
  static bool LoadInterfaces(const rapidjson::Document& document, SyscallLibrary* library);
  static bool LoadStructs(const rapidjson::Document& document, SyscallLibrary* library);

  // TODO(syscall-fidl-transition): A temporary measure during transition that
  // maps the possibly-arbitrary order that the syscalls are in in the JSON IR,
  // and puts them into the order they are in in syscalls.abigen. This is useful
  // so that any listing is diffable for comparing output. This is a temporary
  // assistance for development, and will be removed once transition away from
  // abigen is complete.
  static bool MakeSyscallOrderMatchOldDeclarationOrder(SyscallLibrary* library);
};

#endif  // TOOLS_KAZOO_SYSCALL_LIBRARY_H_
