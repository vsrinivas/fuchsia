// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "tools/fidlcat/lib/code_generator/test_generator.h"

namespace fidlcat {

void CodeGenerator::GenerateIncludes(fidl_codec::PrettyPrinter& printer) {
  printer << "#include <lib/async-loop/cpp/loop.h>\n";
  printer << "#include <lib/async-loop/default.h>\n";
  printer << "#include <lib/async/default.h>\n";
  printer << "#include <lib/syslog/cpp/macros.h>\n";
  printer << '\n';
  printer << "#include <gtest/gtest.h>\n";
  printer << '\n';
  printer << "#include \"lib/sys/cpp/component_context.h\"\n";
  printer << '\n';

  GenerateFidlIncludes(printer);

  printer << '\n';
}

void CodeGenerator::GenerateFidlIncludes(fidl_codec::PrettyPrinter& printer) {
  for (const auto& fidl_include : fidl_headers_) {
    printer << "#include <" << fidl_include << ">\n";
  }
}

std::unique_ptr<FidlCallInfo> OutputEventToFidlCallInfo(OutputEvent* output_event) {
  const Syscall* syscall = output_event->syscall();
  SyscallKind syscall_kind = syscall->kind();

  // We are only interested in FIDL calls
  switch (syscall_kind) {
    case SyscallKind::kChannelRead:
    case SyscallKind::kChannelWrite:
    case SyscallKind::kChannelCall:
      break;
    default:
      return nullptr;
  }

  // We inspect the message to extract
  // "interface name", "method name" and "message content".
  // Based on the system call, this could be in either of output event or invoked event.
  const fidl_codec::InterfaceMethod* method = nullptr;
  zx_txid_t txid;

  switch (syscall_kind) {
    case SyscallKind::kChannelRead:
      method = output_event->GetMessage()->method();
      txid = output_event->GetMessage()->txid();
      break;
    case SyscallKind::kChannelWrite:
    case SyscallKind::kChannelCall:
      method = output_event->invoked_event()->GetMessage()->method();
      txid = output_event->invoked_event()->GetMessage()->txid();
      break;
    default:
      return nullptr;
      break;
  }

  if (method == nullptr) {
    // TODO(nimaj): investigate why this happens for zx_channel_read and zx_channel_write
    // We will not be able to determine method name nor interface name
    return nullptr;
  }

  const fidl_codec::StructValue* decoded_input_value = nullptr;
  const fidl_codec::StructValue* decoded_output_value = nullptr;

  switch (syscall_kind) {
    case SyscallKind::kChannelWrite:
      decoded_input_value = output_event->invoked_event()->GetMessage()->decoded_request();
      if (!decoded_input_value) {
        // monitored process is a server; event is a response sent by server
        decoded_input_value = output_event->invoked_event()->GetMessage()->decoded_response();
      }
      break;
    case SyscallKind::kChannelRead:
      decoded_output_value = output_event->GetMessage()->decoded_response();
      if (!decoded_output_value) {
        // monitored process is a server; event is a request received by the server
        decoded_output_value = output_event->GetMessage()->decoded_request();
      }
      break;
    case SyscallKind::kChannelCall:
      decoded_input_value = output_event->invoked_event()->GetMessage()->decoded_request();
      if (decoded_input_value && output_event->GetMessage() != nullptr) {
        decoded_output_value = output_event->GetMessage()->decoded_response();
      }
      break;
    default:
      return nullptr;
      break;
  }

  // Extract handle information from output event in 2 steps:
  // (1/2) Find handle's struct member
  const fidl_codec::StructMember* handle_member = syscall->SearchInlineMember("handle", true);
  // (2/2) Lookup handle's struct member in invoked_event
  const fidl_codec::HandleValue* handle =
      output_event->invoked_event()->GetHandleValue(handle_member);
  zx_handle_t handle_id = handle->handle().handle;

  bool crashed = output_event->returned_value() == ZX_ERR_PEER_CLOSED;

  return std::make_unique<FidlCallInfo>(
      crashed, method->enclosing_interface().name(), handle_id, txid, syscall_kind, method->name(),
      method->request(), method->response(), decoded_input_value, decoded_output_value);
}

std::string FidlMethodToIncludePath(std::string_view identifier) {
  std::string result = std::string(identifier.substr(0, identifier.find('/')));
  std::replace(result.begin(), result.end(), '.', '/');
  result.append("/cpp/fidl.h");
  return result;
}

std::string ToSnakeCase(std::string_view str) {
  std::ostringstream result;
  std::string separator = "";
  for (char c : str) {
    if (c == '.' || c == '/') {
      result << separator;
    } else if (isupper(c)) {
      result << separator << static_cast<char>(std::tolower(c));
      separator = "";
    } else {
      result << c;
      separator = "_";
    }
  }
  return result.str();
}

}  // namespace fidlcat
