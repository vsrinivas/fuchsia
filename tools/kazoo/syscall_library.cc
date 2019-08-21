// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

#include <zircon/compiler.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/trim.h"
#include "tools/kazoo/alias_workaround.h"
#include "tools/kazoo/output_util.h"

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
  // "zx/" or "zz/".
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
      return fxl::TrimString(fxl::StringView(attrib.GetObject()["value"].GetString()), " \t\n")
          .ToString();
    }
  }
  return std::string();
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
    FXL_CHECK(full_name.substr(0, 3) == "zx/" || full_name.substr(0, 3) == "zz/");
    const std::string name = full_name.substr(3);
    if (name == "duration" || name == "futex" || name == "koid" || name == "paddr" ||
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
    FXL_LOG(ERROR) << "type has no 'kind'";
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
      FXL_CHECK(false) << "TODO: primitive subtype=" << subtype;
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

  FXL_CHECK(false) << "TODO: kind=" << kind;
  return Type();
}

}  // namespace

bool Syscall::HasAttribute(const char* attrib_name) const {
  return attributes_.find(attrib_name) != attributes_.end();
}

std::string Syscall::GetAttribute(const char* attrib_name) const {
  FXL_DCHECK(HasAttribute(attrib_name));
  return attributes_.find(attrib_name)->second;
}

// Converts from FIDL style to C/Kernel style:
// - string to pointer+size
// - vector to pointer+size
// - structs become pointer-to-struct (const on input, mutable on output)
// - etc.
bool Syscall::MapRequestResponseToKernelAbi() {
  FXL_DCHECK(kernel_arguments_.empty());

  // Used for input arguments, which default to const unless alread specified mutable.
  auto default_to_const = [](Constness constness) {
    if (constness == Constness::kUnspecified) {
      return Constness::kConst;
    }
    return constness;
  };

  auto output_optionality =
      [](Optionality optionality) {
        // If explicitly made optional then leave it alone, otherwise mark non-optional.
        if (optionality == Optionality::kOutputOptional) {
          return optionality;
        }
        return Optionality::kOutputNonOptional;
      };

  // Used for output arguments: can't be explicitly const.
  auto ensure_mutable = [](Constness constness) {
    FXL_DCHECK(constness == Constness::kUnspecified || constness == Constness::kMutable);
    return Constness::kMutable;
  };

  auto input_vector_and_string_expand = [&default_to_const](
                                            const StructMember& member,
                                            std::vector<StructMember>* into) {
    const Type& type = member.type();
    if (type.IsVector()) {
      Type pointer_to_subtype(TypePointer(type.DataAsVector().contained_type()),
                              default_to_const(type.constness()), Optionality::kInputArgument);
      into->emplace_back(member.name(), pointer_to_subtype);
      std::string prefix, suffix;
      // If it's a char* or void*, blah_size seems more natural, otherwise, num_blahs is moreso.
      if ((type.DataAsVector().contained_type().IsChar() ||
           type.DataAsVector().contained_type().IsVoid()) &&
          (member.name() != "bytes")) {
        suffix = "_size";
      } else {
        prefix = "num_";
      }
      if (type.DataAsVector().uint32_size()) {
        into->emplace_back(prefix + member.name() + suffix, Type(TypeUint32{}));
      } else {
        into->emplace_back(prefix + member.name() + suffix, Type(TypeSizeT{}));
      }
    } else if (type.IsString()) {
      // char*, using the same constness as the string was specified as.
      into->emplace_back(member.name(),
                         Type(TypePointer(Type(TypeChar{})), default_to_const(type.constness()),
                              Optionality::kInputArgument));
      into->emplace_back(member.name() + "_size", Type(TypeSizeT{}));
    } else {
      // Otherwise, just copy it over.
      into->push_back(member);
    }
  };

  std::vector<StructMember> kernel_request;
  std::vector<StructMember> kernel_response;

  // First, map from FIDL request_/response_ to kernel_request/kernel_response converting string and
  // vectors. At the same time, make all input parameters const (unless specified to be mutable),
  // and ensure output parameters are mutable.
  for (const auto& m : request_.members()) {
    input_vector_and_string_expand(m, &kernel_request);
  }
  for (const auto& m : response_.members()) {
    // Vector and string outputs are currently disallowed, as it's not clear who'd be allocating
    // those (this is typically expressed by a mutable input into which the output is stored).
    FXL_CHECK(!m.type().IsString() && !m.type().IsVector());
    // Otherwise, copy the response member and ensure it's mutable.
    kernel_response.emplace_back(m.name(),
                                 Type(m.type().type_data(), ensure_mutable(m.type().constness()),
                                      output_optionality(m.type().optionality())));
  }

  // Now from these vectors into kernel_arguments_ making pointers to structs as necessary on input
  // (again, with the correct constness).
  for (const auto& m : kernel_request) {
    if (m.type().IsStruct()) {
      // If it's a struct, map to struct*, const unless otherwise specified. The pointer takes the
      // constness of the struct.
      kernel_arguments_.emplace_back(
          m.name(), Type(TypePointer(m.type()), default_to_const(m.type().constness()),
                         Optionality::kInputArgument));
    } else {
      // Otherwise, copy it over, unchanged.
      kernel_arguments_.push_back(m);
    }
  }

  // For output arguments:
  // - Return type is either void or the actual type (we disallow non-simple returns for now, as
  // it's not entirely clear if they should be by pointer or value, and this doesn't happen in
  // current syscalls).
  // - Otherwise, output parameter T is mapped to (mutable) T*.
  if (kernel_response.size() == 0) {
    kernel_return_type_ = Type(TypeVoid{});
  } else {
    kernel_return_type_ = kernel_response[0].type();
    FXL_CHECK(kernel_return_type_.IsSimpleType());
    for (size_t i = 1; i < kernel_response.size(); ++i) {
      const StructMember& m = kernel_response[i];
      kernel_arguments_.emplace_back(
          m.name(), Type(TypePointer(m.type()), ensure_mutable(m.type().constness()),
                         output_optionality(m.type().optionality())));
    }
  }

  // TODO(syscall-fidl-transition): Now that we've got all the arguments in their natural order,
  // honor the "ArgReorder" attribute, which reorders arguments arbitrarily to match existing
  // declaration order.
  return HandleArgReorder();
}

bool Syscall::HandleArgReorder() {
  constexpr const char kReorderAttribName[] = "ArgReorder";
  if (HasAttribute(kReorderAttribName)) {
    const std::string& target_order_string = GetAttribute(kReorderAttribName);
    std::vector<fxl::StringView> target_order =
        fxl::SplitString(target_order_string, ",", fxl::WhiteSpaceHandling::kTrimWhitespace,
                         fxl::SplitResult::kSplitWantAll);
    if (kernel_arguments_.size() != target_order.size()) {
      FXL_LOG(ERROR) << "Attempting to reorder arguments for '" << name() << "', and there's "
                     << kernel_arguments_.size() << " kernel arguments, but " << target_order.size()
                     << " arguments in the reorder spec.";
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
        FXL_LOG(ERROR) << "Attempting to reorder arguments for '" << name() << "', but '" << target
                       << "' wasn't one of the kernel arguments.";
        return false;
      }
    }

    kernel_arguments_ = std::move(new_kernel_arguments);
  }

  return true;
}

void Enum::AddMember(const std::string& member_name, int value) {
  FXL_DCHECK(!HasMember(member_name));
  members_[member_name] = value;
}

bool Enum::HasMember(const std::string& member_name) const {
  return members_.find(member_name) != members_.end();
}

int Enum::ValueForMember(const std::string& member_name) const {
  FXL_CHECK(HasMember(member_name));
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
  return Type();
}

// static
bool SyscallLibraryLoader::FromJson(const std::string& json_ir, SyscallLibrary* library,
                                    bool match_original_order) {
  rapidjson::Document document;
  document.Parse(json_ir);

  // Maybe do schema validation here, though we rely on fidlc for many details
  // and general sanity, so probably only in a diagnostic mode.

  if (!document.IsObject()) {
    FXL_LOG(ERROR) << "Root of json wasn't object.";
    return false;
  }

  library->name_ = document["name"].GetString();
  if (library->name_ != "zz" && library->name_ != "zx") {
    FXL_LOG(ERROR) << "Library name wasn't zz or zx as expected.";
    return false;
  }

  FXL_DCHECK(library->syscalls_.empty());

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

  if (match_original_order && !MakeSyscallOrderMatchOldDeclarationOrder(library)) {
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
    FXL_CHECK(member["value"]["kind"] == "literal") << "TODO: More complex value expressions";
    int member_value = fxl::StringToNumber<int>(
        fxl::StringView(member["value"]["literal"]["value"].GetString()));
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
      FXL_LOG(ERROR) << "Expected Transport to be Syscall.";
      return false;
    }

    std::string interface_name = interface["name"].GetString();
    std::string category = GetCategory(interface, interface_name);

    for (const auto& method : interface["methods"].GetArray()) {
      auto syscall = std::make_unique<Syscall>();
      syscall->id_ = interface_name;
      syscall->original_name_ = method["name"].GetString();
      syscall->category_ = category;
      syscall->name_ =
          category + (category.empty() ? "" : "_") + CamelToSnake(method["name"].GetString());
      syscall->is_noreturn_ = !method["has_response"].GetBool();
      syscall->short_description_ = GetDocAttribute(method);
      if (method.HasMember("maybe_attributes")) {
        for (const auto& attrib : method["maybe_attributes"].GetArray()) {
          syscall->attributes_[attrib["name"].GetString()] = attrib["value"].GetString();
        }
      }

      FXL_CHECK(method["has_request"].GetBool());  // Events are not expected in syscalls.

      auto add_struct_members = [&library](Struct* strukt, const rapidjson::Value& arg) {
        const auto* type_alias = arg.HasMember("experimental_maybe_from_type_alias")
                                     ? &arg["experimental_maybe_from_type_alias"]
                                     : nullptr;
        strukt->members_.emplace_back(arg["name"].GetString(),
                                      TypeFromJson(*library, arg["type"], type_alias));
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

// static
bool SyscallLibraryLoader::MakeSyscallOrderMatchOldDeclarationOrder(SyscallLibrary* library) {
  // During transition, output in the order that the file was originally in to
  // facilitate simple diffing.
  static constexpr const char* kOrderFromOriginalSyscallsAbigen[] = {
      "clock_get",
      "clock_get_monotonic",
      "nanosleep",
      "ticks_get",
      "ticks_per_second",
      "deadline_after",
      "clock_adjust",
      "system_get_dcache_line_size",
      "system_get_num_cpus",
      "system_get_version",
      "system_get_physmem",
      "system_get_features",
      "system_get_event",
      "cache_flush",
      "handle_close",
      "handle_close_many",
      "handle_duplicate",
      "handle_replace",
      "object_wait_one",
      "object_wait_many",
      "object_wait_async",
      "object_signal",
      "object_signal_peer",
      "object_get_property",
      "object_set_property",
      "object_get_info",
      "object_get_child",
      "object_set_profile",
      "channel_create",
      "channel_read",
      "channel_read_etc",
      "channel_write",
      "channel_write_etc",
      "channel_call_noretry",
      "channel_call_finish",
      "channel_call",
      "socket_create",
      "socket_write",
      "socket_read",
      "socket_shutdown",
      "thread_exit",
      "thread_create",
      "thread_start",
      "thread_read_state",
      "thread_write_state",
      "process_exit",
      "process_create",
      "process_start",
      "process_read_memory",
      "process_write_memory",
      "job_create",
      "job_set_policy",
      "task_bind_exception_port",
      "task_suspend",
      "task_suspend_token",
      "task_resume_from_exception",
      "task_create_exception_channel",
      "task_kill",
      "exception_get_thread",
      "exception_get_process",
      "event_create",
      "eventpair_create",
      "futex_wait",
      "futex_wake",
      "futex_requeue",
      "futex_wake_single_owner",
      "futex_requeue_single_owner",
      "futex_get_owner",
      "port_create",
      "port_queue",
      "port_wait",
      "port_cancel",
      "timer_create",
      "timer_set",
      "timer_cancel",
      "vmo_create",
      "vmo_read",
      "vmo_write",
      "vmo_get_size",
      "vmo_set_size",
      "vmo_op_range",
      "vmo_create_child",
      "vmo_set_cache_policy",
      "vmo_replace_as_executable",
      "vmar_allocate",
      "vmar_destroy",
      "vmar_map",
      "vmar_unmap",
      "vmar_protect",
      "cprng_draw_once",
      "cprng_draw",
      "cprng_add_entropy",
      "fifo_create",
      "fifo_read",
      "fifo_write",
      "profile_create",
      "vmar_unmap_handle_close_thread_exit",
      "futex_wake_handle_close_thread_exit",
      "debuglog_create",
      "debuglog_write",
      "debuglog_read",
      "ktrace_read",
      "ktrace_control",
      "ktrace_write",
      "mtrace_control",
      "debug_read",
      "debug_write",
      "debug_send_command",
      "interrupt_create",
      "interrupt_bind",
      "interrupt_wait",
      "interrupt_destroy",
      "interrupt_ack",
      "interrupt_trigger",
      "interrupt_bind_vcpu",
      "ioports_request",
      "ioports_release",
      "vmo_create_contiguous",
      "vmo_create_physical",
      "iommu_create",
      "bti_create",
      "bti_pin",
      "bti_release_quarantine",
      "pmt_unpin",
      "framebuffer_get_info",
      "framebuffer_set_range",
      "pci_get_nth_device",
      "pci_enable_bus_master",
      "pci_reset_device",
      "pci_config_read",
      "pci_config_write",
      "pci_cfg_pio_rw",
      "pci_get_bar",
      "pci_map_interrupt",
      "pci_query_irq_mode",
      "pci_set_irq_mode",
      "pci_init",
      "pci_add_subtract_io_range",
      "pc_firmware_tables",
      "smc_call",
      "resource_create",
      "guest_create",
      "guest_set_trap",
      "vcpu_create",
      "vcpu_resume",
      "vcpu_interrupt",
      "vcpu_read_state",
      "vcpu_write_state",
      "system_mexec",
      "system_mexec_payload_get",
      "system_powerctl",
      "pager_create",
      "pager_create_vmo",
      "pager_detach_vmo",
      "pager_supply_pages",
      "syscall_test_0",
      "syscall_test_1",
      "syscall_test_2",
      "syscall_test_3",
      "syscall_test_4",
      "syscall_test_5",
      "syscall_test_6",
      "syscall_test_7",
      "syscall_test_8",
      "syscall_test_wrapper",
      "syscall_test_handle_create",
  };

  if (library->syscalls_.size() != countof(kOrderFromOriginalSyscallsAbigen)) {
    FXL_LOG(ERROR) << "Have " << library->syscalls_.size() << " syscalls, but original has "
                   << countof(kOrderFromOriginalSyscallsAbigen) << " syscalls.";
    return false;
  }

  std::vector<std::unique_ptr<Syscall>> in_order;

  // TODO(scottmg): This is a crappy linear search done N times, but it's 1) a
  // small N; 2) will be removed once this tool is the standard and we don't use
  // abigen any more.
  for (size_t i = 0; i < countof(kOrderFromOriginalSyscallsAbigen); ++i) {
    for (size_t j = 0; j < library->syscalls_.size(); ++j) {
      if (!library->syscalls_[j]) {
        continue;
      }
      if (library->syscalls_[j]->name() == kOrderFromOriginalSyscallsAbigen[i]) {
        in_order.push_back(std::move(library->syscalls_[j]));
        break;
      }
    }
  }

  library->syscalls_ = std::move(in_order);

  return true;
}
