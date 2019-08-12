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

class SyscallLibrary;

class TypeVoid {};
class TypeBool {};
class TypeChar {};
class TypeUint8 {};
class TypeUint16 {};
class TypeInt32 {};
class TypeUint32 {};
class TypeInt64 {};
class TypeUint64 {};
class TypeSizeT {};
class TypeString {};

class TypeHandle;
class TypeVector;
class TypePointer;
using Type = std::variant<std::monostate, TypeBool, TypeChar, TypeUint8, TypeUint16, TypeInt32,
                          TypeUint32, TypeInt64, TypeUint64, TypeSizeT, TypeHandle, TypeVector,
                          TypeString, TypeVoid, TypePointer>;

Type TypeFromJson(const SyscallLibrary& library, const rapidjson::Value& type);

// Maps a name from typical FidlCamelStyle to zircon_snake_style.
std::string CamelToSnake(const std::string& camel_fidl);

class TypeHandle {
 public:
  explicit TypeHandle(const std::string& handle_type) : handle_type_(handle_type) {}

  const std::string& handle_type() const { return handle_type_; }

 private:
  std::string handle_type_;
};

class TypeVector {
 public:
  explicit TypeVector(const Type& contained_type)
      : contained_type_(std::make_shared<Type>(contained_type)) {}

  const Type& contained_type() const;

 private:
  std::shared_ptr<Type> contained_type_;
};

class TypePointer {
 public:
  explicit TypePointer(const Type& pointed_to_type)
      : pointed_to_type_(std::make_shared<Type>(pointed_to_type)) {}

  const Type& pointed_to_type() const;

 private:
  std::shared_ptr<Type> pointed_to_type_;
};

inline const Type& TypeVector::contained_type() const { return *contained_type_; }
inline const Type& TypePointer::pointed_to_type() const { return *pointed_to_type_; }

class StructMember {
 public:
  StructMember() {}
  StructMember(const std::string& name, const Type& type) : name_(name), type_(type) {}
  ~StructMember() {}

  const std::string& name() const { return name_; }
  const Type& type() const { return type_; }

  bool optional() const { return optional_; }
  void set_optional(bool optional) { optional_ = optional; }

  StructMember CopyAsPointerTo() const {
    StructMember copy = *this;
    copy.type_ = Type(TypePointer(type_));
    return copy;
  }

 private:
  friend class SyscallLibraryLoader;

  std::string name_;
  Type type_;
  bool optional_{false};
};

class Struct {
 public:
  Struct() {}
  ~Struct() {}

  const std::string& name() const { return name_; }
  const std::vector<StructMember>& members() const { return members_; }

 private:
  friend class SyscallLibraryLoader;

  std::string name_;
  std::vector<StructMember> members_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Struct);
};

class Syscall {
 public:
  Syscall() = default;
  ~Syscall() = default;

  const std::string& short_description() const { return short_description_; }
  const std::map<std::string, std::string>& attributes() const { return attributes_; }

  const std::string& original_interface() const { return original_interface_; }
  const std::string& original_name() const { return original_name_; }
  const std::string& category() const { return category_; }
  const std::string& name() const { return name_; }
  bool is_noreturn() const { return is_noreturn_; }
  const Struct& request() const { return request_; }
  const Struct& response() const { return response_; }

  bool HasAttribute(const char* attrib_name) const;

  size_t NumKernelArgs() const;

 private:
  friend class SyscallLibraryLoader;

  std::string original_interface_;  // "zx/Object"
  std::string original_name_;       // "GetInfo"
  std::string category_;            // "object"
  std::string name_;                // "object_get_info"
  std::string short_description_;
  bool is_noreturn_ = false;
  std::map<std::string, std::string> attributes_;
  Struct request_;
  Struct response_;

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
  // TODO(syscall-fidl-transition): A temporary measure during transition that
  // maps the possibly-arbitrary order that the syscalls are in in the JSON IR,
  // and puts them into the order they are in in syscalls.abigen. This is useful
  // so that any listing is diffable for comparing output. This is a temporary
  // assistance for development, and will be removed once transition away from
  // abigen is complete.
  static bool MakeSyscallOrderMatchOldDeclarationOrder(SyscallLibrary* library);
};

#endif  // TOOLS_KAZOO_SYSCALL_LIBRARY_H_
