// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

#include <zircon/compiler.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/ascii.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/trim.h"

namespace {

std::string ToLowerAscii(const std::string& input) {
  std::string ret = input;
  std::transform(ret.begin(), ret.end(), ret.begin(), fxl::ToLowerASCII);
  return ret;
}

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

std::string GetCategory(const rapidjson::Value& interface, const std::string& interface_name) {
  if (interface.HasMember("maybe_attributes")) {
    for (const auto& attrib : interface["maybe_attributes"].GetArray()) {
      if (attrib.GetObject()["name"].Get<std::string>() == "NoProtocolPrefix") {
        return std::string();
      }
    }
  }

  // "zx/" or "zz/".
  constexpr size_t kPrefixLen = 3;
  return ToLowerAscii(
      interface_name.substr(kPrefixLen, interface_name.size() - kPrefixLen));
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

}  // namespace

std::string CamelToSnake(const std::string& camel_fidl) {
  auto is_transition = [](char prev, char cur, char peek) {
    enum { Upper, Lower, Other };
    auto categorize = [](char c) {
      if (c == 0)
        return Other;
      if (c >= 'a' && c <= 'z')
        return Lower;
      if (c >= 'A' && c <= 'Z')
        return Upper;
      if ((c >= '0' && c <= '9') || c == '_')
        return Other;
      FXL_DCHECK(false);
      return Other;
    };
    auto prev_type = categorize(prev);
    auto cur_type = categorize(cur);
    auto peek_type = categorize(peek);

    bool lower_to_upper = prev_type != Upper && cur_type == Upper;
    bool multiple_caps_to_lower =
        peek && (prev_type == Upper && cur_type == Upper && peek_type == Lower);

    return lower_to_upper || multiple_caps_to_lower;
  };
  std::vector<std::string> parts;
  char prev = 0;
  std::string current_word;
  for (size_t i = 0; i < camel_fidl.size(); ++i) {
    char cur = camel_fidl[i];
    char peek = i + 1 < camel_fidl.size() ? camel_fidl[i + 1] : 0;
    if (current_word.size() > 1 && is_transition(prev, cur, peek)) {
      parts.push_back(ToLowerAscii(current_word));
      current_word = cur;
    } else {
      current_word += cur;
    }
    prev = cur;
  }

  if (!current_word.empty()) {
    parts.push_back(ToLowerAscii(current_word));
  }

  return fxl::JoinStrings(parts, "_");
}

Type TypeFromJson(const SyscallLibrary& library, const rapidjson::Value& type) {
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
    } else if (subtype == "usize") {
      return Type(TypeSizeT{});
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
    Type contained_type = TypeFromJson(library, type["element_type"]);
    return Type(TypeVector(contained_type));
  } else if (kind == "string") {
    return Type(TypeString{});
  }

  FXL_CHECK(false) << "TODO: kind=" << kind;
  return Type();
}

bool Syscall::HasAttribute(const char* attrib_name) const {
  return attributes_.find(attrib_name) != attributes_.end();
}

size_t Syscall::NumKernelArgs() const {
  if (is_noreturn()) {
    return request_.members().size();
  }

  // The first return value is passed as the ordinary C return
  // value, but only if there is at least one return value.
  size_t ret_values = response_.members().size();
  if (ret_values > 0) {
    --ret_values;
  }
  return request_.members().size() + ret_values;
}

Type SyscallLibrary::TypeFromIdentifier(const std::string& id) const {
  // TODO: Load struct, enum, union, usings and return one of them here!
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

  for (const auto& interface : document["interface_declarations"].GetArray()) {
    if (!ValidateTransport(interface)) {
      FXL_LOG(ERROR) << "Expected Transport to be Syscall.";
      return false;
    }

    std::string interface_name = interface["name"].GetString();
    std::string category = GetCategory(interface, interface_name);

    for (const auto& method : interface["methods"].GetArray()) {
      auto syscall = std::make_unique<Syscall>();
      syscall->original_interface_ = interface_name;
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
        Type type = TypeFromJson(*library, arg["type"]);
        if (std::holds_alternative<TypeVector>(type)) {
          std::string name(arg["name"].GetString());
          Type subtype(TypePointer(std::get<TypeVector>(type).contained_type()));
          strukt->members_.emplace_back(name, std::move(subtype));
          strukt->members_.emplace_back("num_" + name, Type(TypeSizeT{}));
        } else if (std::holds_alternative<TypeString>(type)) {
          std::string name(arg["name"].GetString());
          Type subtype(TypePointer(Type(TypeChar{})));
          strukt->members_.emplace_back(name, subtype);
          strukt->members_.emplace_back(name + "_size", Type(TypeSizeT{}));
        } else {
          strukt->members_.emplace_back(arg["name"].GetString(), std::move(type));
        }
      };

      Struct& req = syscall->request_;
      req.name_ = syscall->original_name_ + "#request";
      for (const auto& arg : method["maybe_request"].GetArray()) {
        add_struct_members(&req, arg);
      }

      if (method["has_response"].GetBool()) {
        Struct& resp = syscall->response_;
        resp.name_ = syscall->original_name_ + "#response";
        for (const auto& arg : method["maybe_response"].GetArray()) {
          add_struct_members(&resp, arg);
        }
      }

      library->syscalls_.push_back(std::move(syscall));
    }
  }

  if (match_original_order && !MakeSyscallOrderMatchOldDeclarationOrder(library)) {
    return false;
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
