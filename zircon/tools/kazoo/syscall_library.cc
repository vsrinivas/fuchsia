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

using MaybeValue = std::optional<std::reference_wrapper<const rapidjson::Value>>;

// TODO(fxbug.dev/81390): Attribute values may only be string literals for now. Make sure to fix
//  this API once that changes to resolve the constant value for all constant types.
MaybeValue GetConstantValueAsString(const rapidjson::Value& constant) {
  if (constant["kind"] == "literal") {
    return constant["value"];
  }
  return std::nullopt;
}

// Check that an attribute exists, and return true if it has no arguments.
bool HasAttributeWithNoArgs(const rapidjson::Value& element, const std::string& attribute_name) {
  if (!element.HasMember("maybe_attributes")) {
    return false;
  }
  for (const auto& attrib : element["maybe_attributes"].GetArray()) {
    if (CamelToSnake(attrib.GetObject()["name"].Get<std::string>()) == attribute_name) {
      const auto& args = attrib.GetObject()["arguments"];
      if (args.GetArray().Empty()) {
        return true;
      }
    }
  }
  return false;
}

// Check that an attribute exists. If the attribute only has one argument, retrieve that argument's
// value.
MaybeValue GetAttributeStandaloneArgValue(const rapidjson::Value& element,
                                          const std::string& attribute_name) {
  if (!element.HasMember("maybe_attributes")) {
    return std::nullopt;
  }
  for (const auto& attrib : element["maybe_attributes"].GetArray()) {
    if (CamelToSnake(attrib.GetObject()["name"].Get<std::string>()) == attribute_name) {
      const auto& args = attrib.GetObject()["arguments"];
      if (args.Size() == 1) {
        return GetConstantValueAsString(args.GetArray()[0].GetObject()["value"]);
      }
    }
  }
  return std::nullopt;
}

bool ValidateTransport(const rapidjson::Value& protocol) {
  const MaybeValue maybe_value = GetAttributeStandaloneArgValue(protocol, "transport");
  return maybe_value.has_value() && maybe_value.value().get().Get<std::string>() == "Syscall";
}

std::string StripLibraryName(const std::string& full_name) {
  auto prefix_pos = full_name.find_first_of('/');
  ZX_ASSERT_MSG(prefix_pos != full_name.npos, "%s has no library prefix", full_name.c_str());
  size_t prefix_len = prefix_pos + 1;
  return full_name.substr(prefix_len, full_name.size() - prefix_len);
}

std::string GetCategory(const rapidjson::Value& protocol, const std::string& protocol_name) {
  if (protocol.HasMember("maybe_attributes")) {
    for (const auto& attrib : protocol["maybe_attributes"].GetArray()) {
      if (CamelToSnake(attrib.GetObject()["name"].Get<std::string>()) == "no_protocol_prefix") {
        return std::string();
      }
    }
  }

  return ToLowerAscii(StripLibraryName(protocol_name));
}

std::string GetDocAttribute(const rapidjson::Value& method) {
  const MaybeValue maybe_value = GetAttributeStandaloneArgValue(method, "doc");
  if (maybe_value.has_value()) {
    return maybe_value.value().get().GetString();
  }
  return std::string();
}

Required GetRequiredAttribute(const rapidjson::Value& field) {
  return HasAttributeWithNoArgs(field, "required") ? Required::kYes : Required::kNo;
}

std::vector<std::string> GetCleanDocAttribute(const std::string& full_doc_attribute) {
  auto lines = SplitString(full_doc_attribute, '\n', kTrimWhitespace);
  if (!lines.empty() && lines[lines.size() - 1].empty()) {
    lines.pop_back();
  }
  std::for_each(lines.begin(), lines.end(),
                [](std::string& line) { return TrimString(line, " \t\n"); });
  return lines;
}

std::optional<Type> PrimitiveTypeFromName(std::string subtype) {
  if (subtype == "int8") {
    return Type(TypeInt8{});
  }
  if (subtype == "uint8") {
    return Type(TypeUint8{});
  }
  if (subtype == "int16") {
    return Type(TypeInt16{});
  }
  if (subtype == "uint16") {
    return Type(TypeUint16{});
  }
  if (subtype == "int32") {
    return Type(TypeInt32{});
  }
  if (subtype == "uint32") {
    return Type(TypeUint32{});
  }
  if (subtype == "int64") {
    return Type(TypeInt64{});
  }
  if (subtype == "uint64") {
    return Type(TypeUint64{});
  }
  if (subtype == "bool") {
    return Type(TypeBool{});
  }
  if (subtype == "char") {
    return Type(TypeChar{});
  }

  return std::nullopt;
}

Type TypeFromJson(const SyscallLibrary& library, const rapidjson::Value& type,
                  const rapidjson::Value* alias) {
  if (alias) {
    // If the "experimental_maybe_from_alias" field is non-null, then the source-level has used
    // a type that's declared as "using x = y;". Here, treat various "x"s as special types. This
    // is likely mostly (?) temporary until there's 1) a more nailed down alias implementation in
    // the front end (fidlc) and 2) we move various parts of zx.fidl from being built-in to fidlc to
    // actual source level fidl and shared between the syscall definitions and normal FIDL.
    const std::string full_name((*alias)["name"].GetString());
    if (full_name.substr(0, 3) == "zx/") {
      const std::string name = full_name.substr(3);
      if (name == "duration" || name == "Futex" || name == "koid" || name == "paddr" ||
          name == "rights" || name == "signals" || name == "status" || name == "time" ||
          name == "ticks" || name == "vaddr" || name == "VmOption") {
        return Type(TypeZxBasicAlias(CamelToSnake(name)));
      }
    }

    const std::string name = StripLibraryName(full_name);
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

    return library.TypeFromIdentifier(full_name);
  }

  if (!type.HasMember("kind")) {
    fprintf(stderr, "type has no 'kind'\n");
    return Type();
  }

  std::string kind = type["kind"].GetString();
  if (kind == "primitive") {
    const rapidjson::Value& subtype_value = type["subtype"];
    ZX_ASSERT(subtype_value.IsString());
    std::string subtype = subtype_value.GetString();
    return PrimitiveTypeFromName(subtype).value();
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
  return attributes_.find(CamelToSnake(attrib_name)) != attributes_.end();
}

std::string Syscall::GetAttribute(const char* attrib_name) const {
  ZX_ASSERT(HasAttribute(attrib_name));
  return attributes_.find(CamelToSnake(attrib_name))->second;
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
          m.name(),
          Type(TypePointer(Type(TypeChar{})), default_to_const(type.constness()),
               Optionality::kInputArgument),
          m.attributes());
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
      kernel_arguments_.emplace_back(
          m.name(),
          Type(TypePointer(type), Constness::kMutable, output_optionality(type.optionality())),
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

void Enum::AddMember(const std::string& member_name, EnumMember member) {
  ZX_ASSERT(!HasMember(member_name));
  members_[member_name] = std::move(member);
  insertion_order_.push_back(member_name);
}

bool Enum::HasMember(const std::string& member_name) const {
  return members_.find(member_name) != members_.end();
}

const EnumMember& Enum::ValueForMember(const std::string& member_name) const {
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

  for (const auto& alias : aliases_) {
    if (alias->id() == id) {
      return Type(TypeAlias(alias.get()));
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

Type SyscallLibrary::TypeFromName(const std::string& name) const {
  if (auto primitive = PrimitiveTypeFromName(name); primitive.has_value()) {
    return primitive.value();
  }
  return TypeFromIdentifier(name);
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
    fprintf(stderr, "Incorrect fidlc JSON IR, wasn't json object.\n");
    return false;
  }

  library->name_ = document["name"].GetString();
  if (library->name_ != "zx" && library->name_ != "zxio") {
    fprintf(stderr, "Library name %s wasn't zx or zxio as expected.\n", library->name_.c_str());
    return false;
  }

  ZX_ASSERT(library->syscalls_.empty());

  // The order of these loads is significant. For example, enums must be loaded to be able to be
  // referred to by protocol methods.

  if (!LoadBits(document, library)) {
    return false;
  }

  if (!LoadEnums(document, library)) {
    return false;
  }

  if (!LoadAliases(document, library)) {
    return false;
  }

  if (!LoadStructs(document, library)) {
    return false;
  }

  if (!LoadTables(document, library)) {
    return false;
  }

  if (!LoadProtocols(document, library)) {
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
  std::string stripped = StripLibraryName(full_name);
  obj->original_name_ = stripped;
  obj->base_name_ = CamelToSnake(stripped);
  std::string doc_attribute = GetDocAttribute(json);
  obj->description_ = GetCleanDocAttribute(doc_attribute);
  const rapidjson::Value& type = json["type"];
  if (type.IsString()) {
    // Enum
    obj->underlying_type_ = PrimitiveTypeFromName(type.GetString()).value();
  } else {
    ZX_ASSERT(type.IsObject());
    // Bits
    ZX_ASSERT_MSG(type["kind"].GetString() == std::string("primitive"),
                  "Enum %s not backed by primitive type", full_name.c_str());
    const rapidjson::Value& subtype_value = type["subtype"];
    ZX_ASSERT(subtype_value.IsString());
    std::string subtype = subtype_value.GetString();
    obj->underlying_type_ = PrimitiveTypeFromName(subtype).value();
  }
  for (const auto& member : json["members"].GetArray()) {
    ZX_ASSERT_MSG(member["value"]["kind"] == "literal", "TODO: More complex value expressions");
    uint64_t member_value;
    std::string decimal = member["value"]["literal"]["value"].GetString();
    if (obj->underlying_type().IsUnsignedInt()) {
      member_value = StringToUInt(decimal);
    } else if (obj->underlying_type().IsSignedInt()) {
      member_value = StringToInt(decimal);
    } else {
      ZX_PANIC("Unreachable");
    }
    std::string doc_attribute = GetDocAttribute(member);
    obj->AddMember(
        member["name"].GetString(),
        EnumMember{.value = member_value, .description = GetCleanDocAttribute(doc_attribute)});
  }
  return obj;
}

// static
bool SyscallLibraryLoader::ExtractPayload(Struct& payload, const std::string& type_name,
                                          const rapidjson::Document& document,
                                          SyscallLibrary* library) {
  auto FindStructDecl = [&](const rapidjson::Value& struct_list) -> bool {
    for (const auto& struct_json : struct_list.GetArray()) {
      std::string struct_name = struct_json["name"].GetString();
      if (struct_name == type_name) {
        for (const auto& arg : struct_json["members"].GetArray()) {
          Struct* strukt = &payload;
          const auto* alias = arg.HasMember("experimental_maybe_from_alias")
                                  ? &arg["experimental_maybe_from_alias"]
                                  : nullptr;
          strukt->members_.emplace_back(arg["name"].GetString(),
                                        TypeFromJson(*library, arg["type"], alias),
                                        std::map<std::string, std::string>{});
          if (arg.HasMember("maybe_attributes")) {
            for (const auto& attrib : arg["maybe_attributes"].GetArray()) {
              const auto attrib_name = attrib["name"].GetString();
              const MaybeValue maybe_value = GetAttributeStandaloneArgValue(arg, attrib_name);
              strukt->members_.back().attributes_[CamelToSnake(attrib_name)] =
                  maybe_value.has_value() && maybe_value.value().get().IsString()
                      ? maybe_value.value().get().GetString()
                      : "";
            }
          }
        }

        return true;
      }
    }

    return false;
  };

  return FindStructDecl(document["struct_declarations"]) ||
         FindStructDecl(document["external_struct_declarations"]);
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
bool SyscallLibraryLoader::LoadProtocols(const rapidjson::Document& document,
                                         SyscallLibrary* library) {
  for (const auto& protocol : document["protocol_declarations"].GetArray()) {
    if (!ValidateTransport(protocol)) {
      fprintf(stderr, "Expected Transport to be Syscall.\n");
      return false;
    }

    std::string protocol_name = protocol["name"].GetString();
    std::string category = GetCategory(protocol, protocol_name);
    bool protocol_prefix = !HasAttributeWithNoArgs(protocol, "no_protocol_prefix");

    for (const auto& method : protocol["methods"].GetArray()) {
      auto syscall = std::make_unique<Syscall>();
      syscall->id_ = protocol_name;
      syscall->original_name_ = method["name"].GetString();
      syscall->category_ = category;
      std::string snake_name = CamelToSnake(method["name"].GetString());
      if (protocol_prefix) {
        syscall->name_ = category + "_" + snake_name;
      } else {
        syscall->name_ = snake_name;
      }
      syscall->is_noreturn_ = !method["has_response"].GetBool();
      const auto doc_attribute = GetDocAttribute(method);
      if (method.HasMember("maybe_attributes")) {
        for (const auto& attrib : method["maybe_attributes"].GetArray()) {
          const auto attrib_name = attrib["name"].GetString();
          const MaybeValue maybe_value = GetAttributeStandaloneArgValue(method, attrib_name);
          syscall->attributes_[CamelToSnake(attrib_name)] =
              maybe_value.has_value() && maybe_value.value().get().IsString()
                  ? maybe_value.value().get().GetString()
                  : "";
        }
      }

      ZX_ASSERT(method["has_request"].GetBool());  // Events are not expected in syscalls.

      Struct& req = syscall->request_;
      req.id_ = syscall->original_name_ + "#request";
      if (method.HasMember("maybe_request_payload")) {
        if (!ExtractPayload(req, method["maybe_request_payload"]["identifier"].GetString(),
                            document, library)) {
          return false;
        }
      }

      if (method["has_response"].GetBool()) {
        Struct& resp = syscall->response_;
        resp.id_ = syscall->original_name_ + "#response";
        if (method.HasMember("maybe_response_success_type")) {
          if (!ExtractPayload(resp, method["maybe_response_success_type"]["identifier"].GetString(),
                              document, library)) {
            return false;
          }
        } else if (method.HasMember("maybe_response_payload")) {
          if (!ExtractPayload(resp, method["maybe_response_payload"]["identifier"].GetString(),
                              document, library)) {
            return false;
          }
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
bool SyscallLibraryLoader::LoadAliases(const rapidjson::Document& document,
                                       SyscallLibrary* library) {
  for (const auto& alias_json : document["alias_declarations"].GetArray()) {
    auto obj = std::make_unique<Alias>();
    std::string full_name = alias_json["name"].GetString();
    obj->id_ = full_name;
    std::string stripped = StripLibraryName(full_name);
    obj->original_name_ = stripped;
    obj->base_name_ = CamelToSnake(stripped);
    const rapidjson::Value& partial_type_ctor = alias_json["partial_type_ctor"];
    ZX_ASSERT(partial_type_ctor.IsObject());
    obj->partial_type_ctor_ = partial_type_ctor["name"].GetString();
    std::string doc_attribute = GetDocAttribute(alias_json);
    obj->description_ = GetCleanDocAttribute(doc_attribute);
    library->aliases_.push_back(std::move(obj));
  }
  return true;
}

// static
bool SyscallLibraryLoader::LoadStructs(const rapidjson::Document& document,
                                       SyscallLibrary* library) {
  // TODO(scottmg): In transition, we're still relying on the existing Zircon headers to define all
  // these structures. So we only load their names for the time being, which is enough for now to
  // know that there's something in the .fidl file where the struct is declared. Note also that
  // protocol parsing fills out request/response "structs", so that code should likely be shared
  // when this is implemented.
  for (const auto& struct_json : document["struct_declarations"].GetArray()) {
    auto obj = std::make_unique<Struct>();
    std::string full_name = struct_json["name"].GetString();
    obj->id_ = full_name;
    std::string stripped = StripLibraryName(full_name);
    obj->original_name_ = stripped;
    obj->base_name_ = CamelToSnake(stripped);
    library->structs_.push_back(std::move(obj));
  }
  return true;
}

// static
bool SyscallLibraryLoader::LoadTables(const rapidjson::Document& document,
                                      SyscallLibrary* library) {
  for (const auto& json : document["table_declarations"].GetArray()) {
    auto obj = std::make_unique<Table>();
    std::string full_name = json["name"].GetString();
    obj->id_ = full_name;
    std::string stripped = StripLibraryName(full_name);
    obj->original_name_ = stripped;
    obj->base_name_ = CamelToSnake(stripped);
    std::string doc_attribute = GetDocAttribute(json);
    obj->description_ = GetCleanDocAttribute(doc_attribute);
    std::vector<TableMember> members;
    for (const auto& member : json["members"].GetArray()) {
      std::string name = member["name"].GetString();
      const auto* alias = member.HasMember("experimental_maybe_from_alias")
                              ? &member["experimental_maybe_from_alias"]
                              : nullptr;
      Type type = TypeFromJson(*library, member["type"], alias);
      Required required = GetRequiredAttribute(member);
      std::string doc_attribute = GetDocAttribute(member);
      std::vector<std::string> description = GetCleanDocAttribute(doc_attribute);
      members.emplace_back(std::move(name), std::move(type), std::move(description), required);
    }
    obj->members_ = std::move(members);
    library->tables_.push_back(std::move(obj));
  }
  return true;
}
