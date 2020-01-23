// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <algorithm>

#include "tools/kazoo/alias_workaround.h"
#include "tools/kazoo/output_util.h"
#include "tools/kazoo/string_util.h"

namespace {

bool ValidateTransport(const rapidjson::Value& interface) {
  if (!interface.HasMember("maybe_attributes")) {
    return false;
  }
  for (const auto& attrib : interface["maybe_attributes"].GetArray()) {
    if (attrib.GetObject()["name"].Get<std::string>() == "Transport") {
      if (attrib.GetObject()["value"].Get<std::string>() == "Syscall") {
        return true;
      }
    }
  }
  return false;
}

std::string StripLibraryName(const std::string& full_name) {
  // "zx/".
  constexpr size_t kPrefixLen = 3;
  return full_name.substr(kPrefixLen, full_name.size() - kPrefixLen);
}

// Converts a type name to Zircon style: In particular, this converts the basic name to snake_case,
// and then wraps it in "zx_" and "_t". For example, HandleInfo -> "zx_handle_info_t".
std::string TypeNameToZirconStyle(const std::string& base_name) {
  return "zx_" + CamelToSnake(base_name) + "_t";
}

std::string GetCategory(const rapidjson::Value& interface, const std::string& interface_name) {
  if (interface.HasMember("maybe_attributes")) {
    for (const auto& attrib : interface["maybe_attributes"].GetArray()) {
      if (attrib.GetObject()["name"].Get<std::string>() == "NoProtocolPrefix") {
        return std::string();
      }
    }
  }

  return ToLowerAscii(StripLibraryName(interface_name));
}

std::string GetDocAttribute(const rapidjson::Value& method) {
  if (!method.HasMember("maybe_attributes")) {
    return std::string();
  }
  for (const auto& attrib : method["maybe_attributes"].GetArray()) {
    if (attrib.GetObject()["name"].Get<std::string>() == "Doc") {
      return attrib.GetObject()["value"].GetString();
    }
  }
  return std::string();
}

constexpr char kRightsPrefix[] = " Rights: ";

std::string GetShortDescriptionFromDocAttribute(const std::string& full_doc_attribute) {
  auto lines = SplitString(full_doc_attribute, '\n', kKeepWhitespace);
  if (lines.size() < 1 || lines[0].substr(0, strlen(kRightsPrefix)) == kRightsPrefix) {
    return std::string();
  }
  return TrimString(lines[0], " \t\n");
}

std::vector<std::string> GetRightsSpecsFromDocAttribute(const std::string& full_doc_attribute) {
  auto lines = SplitString(full_doc_attribute, '\n', kKeepWhitespace);
  std::vector<std::string> ret;
  for (const auto& line : lines) {
    if (line.substr(0, strlen(kRightsPrefix)) == kRightsPrefix) {
      ret.push_back(line.substr(strlen(kRightsPrefix)));
    }
  }

  return ret;
}

Type TypeFromJson(const SyscallLibrary& library, const rapidjson::Value& type,
                  const rapidjson::Value* type_alias) {
  if (type_alias) {
    // If the "experimental_maybe_from_type_alias" field is non-null, then the source-level has used
    // a type that's declared as "using x = y;". Here, treat various "x"s as special types. This
    // is likely mostly (?) temporary until there's 1) a more nailed down alias implementation in
    // the front end (fidlc) and 2) we move various parts of zx.fidl from being built-in to fidlc to
    // actual source level fidl and shared between the syscall definitions and normal FIDL.
    const std::string full_name(type_alias->operator[]("name").GetString());
    ZX_ASSERT(full_name.substr(0, 3) == "zx/");
    const std::string name = full_name.substr(3);
    if (name == "duration" || name == "Futex" || name == "koid" || name == "paddr" ||
        name == "rights" || name == "signals" || name == "status" || name == "time" ||
        name == "ticks" || name == "vaddr" || name == "VmOption") {
      return Type(TypeZxBasicAlias(CamelToSnake(name)));
    }

    if (name == "uintptr") {
      return Type(TypeUintptrT{});
    }

    if (name == "usize") {
      return Type(TypeSizeT{});
    }

    Type workaround_type;
    if (AliasWorkaround(name, library, &workaround_type)) {
      return workaround_type;
    }
  }

  if (!type.HasMember("kind")) {
    fprintf(stderr, "type has no 'kind'\n");
    return Type();
  }

  std::string kind = type["kind"].GetString();
  if (kind == "primitive") {
    std::string subtype = type["subtype"].GetString();
    if (subtype == "uint8") {
      return Type(TypeUint8{});
    } else if (subtype == "uint16") {
      return Type(TypeUint16{});
    } else if (subtype == "int32") {
      return Type(TypeInt32{});
    } else if (subtype == "uint32") {
      return Type(TypeUint32{});
    } else if (subtype == "int64") {
      return Type(TypeInt64{});
    } else if (subtype == "uint64") {
      return Type(TypeUint64{});
    } else if (subtype == "bool") {
      return Type(TypeBool{});
    } else {
      ZX_ASSERT_MSG(false, "TODO: primitive subtype %s", subtype.c_str());
    }
  } else if (kind == "identifier") {
    std::string id = type["identifier"].GetString();
    return library.TypeFromIdentifier(type["identifier"].GetString());
  } else if (kind == "handle") {
    return Type(TypeHandle(type["subtype"].GetString()));
  } else if (kind == "vector") {
    Type contained_type = TypeFromJson(library, type["element_type"], nullptr);
    return Type(TypeVector(contained_type));
  } else if (kind == "string") {
    return Type(TypeString{});
  }

  ZX_ASSERT_MSG(false, "TODO: kind=%s", kind.c_str());
  return Type();
}

}  // namespace

bool Syscall::HasAttribute(const char* attrib_name) const {
  return attributes_.find(attrib_name) != attributes_.end();
}

std::string Syscall::GetAttribute(const char* attrib_name) const {
  ZX_ASSERT(HasAttribute(attrib_name));
  return attributes_.find(attrib_name)->second;
}

// Converts from FIDL style to C/Kernel style:
// - string to pointer+size
// - vector to pointer+size
// - structs become pointer-to-struct (const on input, mutable on output)
// - etc.
bool Syscall::MapRequestResponseToKernelAbi() {
  ZX_ASSERT(kernel_arguments_.empty());

  // Used for input arguments, which default to const unless alread specified mutable.
  auto default_to_const = [](Constness constness) {
    if (constness == Constness::kUnspecified) {
      return Constness::kConst;
    }
    return constness;
  };

  auto output_optionality = [](Optionality optionality) {
    // If explicitly made optional then leave it alone, otherwise mark non-optional.
    if (optionality == Optionality::kOutputOptional) {
      return optionality;
    }
    return Optionality::kOutputNonOptional;
  };

  auto get_vector_size_name = [](const StructMember& member) {
    std::string prefix, suffix;
    // If it's a char* or void*, blah_size seems more natural, otherwise, num_blahs is moreso.
    if ((member.type().DataAsVector().contained_type().IsChar() ||
         member.type().DataAsVector().contained_type().IsVoid()) &&
        (member.name() != "bytes")) {
      suffix = "_size";
    } else {
      prefix = "num_";
    }
    return std::tuple<std::string, bool>(prefix + member.name() + suffix,
                                         member.type().DataAsVector().uint32_size());
  };

  // Map inputs first, converting vectors, strings, and structs to their corresponding input types
  // as we go.
  for (const auto& m : request_.members()) {
    const Type& type = m.type();
    if (type.IsVector()) {
      Type pointer_to_subtype(
          TypePointer(type.DataAsVector().contained_type(), IsDecayedVectorTag{}),
          default_to_const(type.constness()), Optionality::kInputArgument);
      kernel_arguments_.emplace_back(m.name(), pointer_to_subtype, m.attributes());
      auto [size_name, is_u32] = get_vector_size_name(m);
      kernel_arguments_.emplace_back(size_name, is_u32 ? Type(TypeUint32{}) : Type(TypeSizeT{}),
                                     std::map<std::string, std::string>{});
    } else if (type.IsString()) {
      // char*, using the same constness as the string was specified as.
      kernel_arguments_.emplace_back(
          m.name(), Type(TypePointer(Type(TypeChar{})), default_to_const(type.constness()),
                         Optionality::kInputArgument), m.attributes());
      kernel_arguments_.emplace_back(m.name() + "_size", Type(TypeSizeT{}),
                                     std::map<std::string, std::string>{});
    } else if (type.IsStruct()) {
      // If it's a struct, map to struct*, const unless otherwise specified. The pointer takes the
      // constness of the struct.
      kernel_arguments_.emplace_back(
          m.name(),
          Type(TypePointer(type), default_to_const(type.constness()), Optionality::kInputArgument),
          m.attributes());
    } else {
      // Otherwise, copy it over, unchanged other than to tag it as input.
      kernel_arguments_.emplace_back(m.name(),
                                     Type(type.type_data(), default_to_const(m.type().constness()),
                                          Optionality::kInputArgument),
                                     m.attributes());
    }
  }

  // Similarly for the outputs, but turning buffers into outparams, and with special handling for
  // the C return value.
  size_t start_at;
  if (response_.members().size() == 0 || !response_.members()[0].type().IsSimpleType()) {
    kernel_return_type_ = Type(TypeVoid{});
    start_at = 0;
  } else {
    kernel_return_type_ = response_.members()[0].type();
    start_at = 1;
  }
  for (size_t i = start_at; i < response_.members().size(); ++i) {
    const StructMember& m = response_.members()[i];
    const Type& type = m.type();
    if (type.IsVector()) {
      // TODO(syscall-fidl-transition): These vector types aren't marked as non-optional in
      // abigen, but generally they probably are.
      Type pointer_to_subtype(
          TypePointer(type.DataAsVector().contained_type(), IsDecayedVectorTag{}),
          Constness::kMutable, Optionality::kOutputOptional);
      kernel_arguments_.emplace_back(m.name(), pointer_to_subtype, m.attributes());
      auto [size_name, is_u32] = get_vector_size_name(m);
      kernel_arguments_.emplace_back(size_name, is_u32 ? Type(TypeUint32{}) : Type(TypeSizeT{}),
                                     std::map<std::string, std::string>{});
    } else if (type.IsString()) {
      kernel_arguments_.emplace_back(
          m.name(),
          Type(TypePointer(Type(TypeChar{})), Constness::kMutable, Optionality::kOutputOptional),
          m.attributes());
      kernel_arguments_.emplace_back(m.name() + "_size", Type(TypeSizeT{}),
                                     std::map<std::string, std::string>{});
    } else if (type.IsPointer()) {
      kernel_arguments_.emplace_back(
          m.name(), Type(type.type_data(), Constness::kMutable, Optionality::kOutputOptional),
          m.attributes());
    } else {
      // Everything else becomes a T* (to make it an out parameter).
      kernel_arguments_.emplace_back(m.name(), Type(TypePointer(type), Constness::kMutable,
                                                    output_optionality(type.optionality())),
                                     m.attributes());
    }
  }

  // Now that we've got all the arguments in their natural order, honor the
  // "ArgReorder" attribute, which reorders arguments arbitrarily to match
  // existing declaration order.
  return HandleArgReorder();
}

bool Syscall::HandleArgReorder() {
  constexpr const char kReorderAttribName[] = "ArgReorder";
  if (HasAttribute(kReorderAttribName)) {
    const std::string& target_order_string = GetAttribute(kReorderAttribName);
    std::vector<std::string> target_order = SplitString(target_order_string, ',', kTrimWhitespace);
    if (kernel_arguments_.size() != target_order.size()) {
      fprintf(stderr,
              "Attempting to reorder arguments for '%s', and there's %zu kernel arguments, but %zu "
              "arguments in the reorder spec.\n",
              name().c_str(), kernel_arguments_.size(), target_order.size());
      return false;
    }

    std::vector<StructMember> new_kernel_arguments;
    for (const auto& target : target_order) {
      bool found = false;
      for (const auto& ka : kernel_arguments_) {
        if (ka.name() == target) {
          new_kernel_arguments.push_back(ka);
          found = true;
          break;
        }
      }

      if (!found) {
        fprintf(stderr,
                "Attempting to reorder arguments for '%s', but '%s' wasn't one of the kernel "
                "arguments.\n",
                name().c_str(), target.c_str());
        return false;
      }
    }

    kernel_arguments_ = std::move(new_kernel_arguments);
  }

  return true;
}

void Enum::AddMember(const std::string& member_name, int value) {
  ZX_ASSERT(!HasMember(member_name));
  members_[member_name] = value;
}

bool Enum::HasMember(const std::string& member_name) const {
  return members_.find(member_name) != members_.end();
}

int Enum::ValueForMember(const std::string& member_name) const {
  ZX_ASSERT(HasMember(member_name));
  return members_.find(member_name)->second;
}

Type SyscallLibrary::TypeFromIdentifier(const std::string& id) const {
  for (const auto& bits : bits_) {
    if (bits->id() == id) {
      // TODO(scottmg): Consider if we need to separate bits from enum here.
      return Type(TypeEnum{bits.get()});
    }
  }

  for (const auto& enm : enums_) {
    if (enm->id() == id) {
      return Type(TypeEnum{enm.get()});
    }
  }

  for (const auto& strukt : structs_) {
    if (strukt->id() == id) {
      return Type(TypeStruct(strukt.get()));
    }
  }

  // TODO: Load struct, union, usings and return one of them here!
  ZX_ASSERT_MSG(false, "unhandled TypeFromIdentifier for %s", id.c_str());
  return Type();
}

void SyscallLibrary::FilterSyscalls(const std::set<std::string>& attributes_to_exclude) {
  std::vector<std::unique_ptr<Syscall>> filtered;
  for (auto& syscall : syscalls_) {
    if (std::any_of(
            attributes_to_exclude.begin(), attributes_to_exclude.end(),
            [&syscall](const std::string& x) { return syscall->HasAttribute(x.c_str()); })) {
      continue;
    }

    filtered.push_back(std::move(syscall));
  }

  syscalls_ = std::move(filtered);
}

// static
bool SyscallLibraryLoader::FromJson(const std::string& json_ir, SyscallLibrary* library) {
  rapidjson::Document document;
  document.Parse(json_ir);

  // Maybe do schema validation here, though we rely on fidlc for many details
  // and general sanity, so probably only in a diagnostic mode.

  if (!document.IsObject()) {
    fprintf(stderr, "Root of json wasn't object.\n");
    return false;
  }

  library->name_ = document["name"].GetString();
  if (library->name_ != "zx") {
    fprintf(stderr, "Library name wasn't zx as expected.\n");
    return false;
  }

  ZX_ASSERT(library->syscalls_.empty());

  // The order of these loads is significant. For example, enums must be loaded to be able to be
  // referred to by interface methods.

  if (!LoadBits(document, library)) {
    return false;
  }

  if (!LoadEnums(document, library)) {
    return false;
  }

  if (!LoadStructs(document, library)) {
    return false;
  }

  if (!LoadInterfaces(document, library)) {
    return false;
  }

  return true;
}

// 'bits' are currently handled the same as enums, so just use Enum for now as the underlying
// data storage.
//
// static
std::unique_ptr<Enum> SyscallLibraryLoader::ConvertBitsOrEnumMember(const rapidjson::Value& json) {
  auto obj = std::make_unique<Enum>();
  std::string full_name = json["name"].GetString();
  obj->id_ = full_name;
  obj->original_name_ = StripLibraryName(full_name);
  obj->name_ = TypeNameToZirconStyle(obj->original_name_);
  for (const auto& member : json["members"].GetArray()) {
    ZX_ASSERT_MSG(member["value"]["kind"] == "literal", "TODO: More complex value expressions");
    int member_value = StringToInt(member["value"]["literal"]["value"].GetString());
    obj->AddMember(member["name"].GetString(), member_value);
  }
  return obj;
}

// static
bool SyscallLibraryLoader::LoadBits(const rapidjson::Document& document, SyscallLibrary* library) {
  for (const auto& bits_json : document["bits_declarations"].GetArray()) {
    library->bits_.push_back(ConvertBitsOrEnumMember(bits_json));
  }
  return true;
}

// static
bool SyscallLibraryLoader::LoadEnums(const rapidjson::Document& document, SyscallLibrary* library) {
  for (const auto& enum_json : document["enum_declarations"].GetArray()) {
    library->enums_.push_back(ConvertBitsOrEnumMember(enum_json));
  }
  return true;
}

// static
bool SyscallLibraryLoader::LoadInterfaces(const rapidjson::Document& document,
                                          SyscallLibrary* library) {
  for (const auto& interface : document["interface_declarations"].GetArray()) {
    if (!ValidateTransport(interface)) {
      fprintf(stderr, "Expected Transport to be Syscall.\n");
      return false;
    }

    std::string interface_name = interface["name"].GetString();
    std::string category = GetCategory(interface, interface_name);

    for (const auto& method : interface["methods"].GetArray()) {
      auto syscall = std::make_unique<Syscall>();
      syscall->id_ = interface_name;
      syscall->original_name_ = method["name"].GetString();
      syscall->category_ = category;
      std::string snake_name = CamelToSnake(method["name"].GetString());
      if (!StartsWith(snake_name, category)) {
        snake_name = category + (category.empty() ? "" : "_") + snake_name;
      }
      syscall->name_ = snake_name;
      syscall->is_noreturn_ = !method["has_response"].GetBool();
      const auto doc_attribute = GetDocAttribute(method);
      syscall->short_description_ = GetShortDescriptionFromDocAttribute(doc_attribute);
      syscall->rights_specs_ = GetRightsSpecsFromDocAttribute(doc_attribute);
      if (method.HasMember("maybe_attributes")) {
        for (const auto& attrib : method["maybe_attributes"].GetArray()) {
          syscall->attributes_[attrib["name"].GetString()] = attrib["value"].GetString();
        }
      }

      ZX_ASSERT(method["has_request"].GetBool());  // Events are not expected in syscalls.

      auto add_struct_members = [&library](Struct* strukt, const rapidjson::Value& arg) {
        const auto* type_alias = arg.HasMember("experimental_maybe_from_type_alias")
                                     ? &arg["experimental_maybe_from_type_alias"]
                                     : nullptr;
        strukt->members_.emplace_back(arg["name"].GetString(),
                                      TypeFromJson(*library, arg["type"], type_alias),
                                      std::map<std::string, std::string>{});
        if (arg.HasMember("maybe_attributes")) {
          for (const auto& attrib : arg["maybe_attributes"].GetArray()) {
            strukt->members_.back().attributes_[attrib["name"].GetString()] =
                attrib["value"].GetString();
          }
        }
      };

      Struct& req = syscall->request_;
      req.id_ = syscall->original_name_ + "#request";
      for (const auto& arg : method["maybe_request"].GetArray()) {
        add_struct_members(&req, arg);
      }

      if (method["has_response"].GetBool()) {
        Struct& resp = syscall->response_;
        resp.id_ = syscall->original_name_ + "#response";
        for (const auto& arg : method["maybe_response"].GetArray()) {
          add_struct_members(&resp, arg);
        }
      }

      if (!syscall->MapRequestResponseToKernelAbi()) {
        return false;
      }

      library->syscalls_.push_back(std::move(syscall));
    }
  }

  return true;
}

// static
bool SyscallLibraryLoader::LoadStructs(const rapidjson::Document& document,
                                       SyscallLibrary* library) {
  // TODO(scottmg): In transition, we're still relying on the existing Zircon headers to define all
  // these structures. So we only load their names for the time being, which is enough for now to
  // know that there's something in the .fidl file where the struct is declared. Note also that
  // interface parsing fills out request/response "structs", so that code should likely be shared
  // when this is implemented.
  for (const auto& struct_json : document["struct_declarations"].GetArray()) {
    auto obj = std::make_unique<Struct>();
    std::string full_name = struct_json["name"].GetString();
    obj->id_ = full_name;
    obj->original_name_ = StripLibraryName(full_name);
    obj->name_ = TypeNameToZirconStyle(obj->original_name_);
    library->structs_.push_back(std::move(obj));
  }
  return true;
}
