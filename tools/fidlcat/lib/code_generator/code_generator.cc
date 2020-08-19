#include "tools/fidlcat/lib/code_generator/test_generator.h"

namespace fidlcat {

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
  const fidl_codec::FidlMessageValue* message = nullptr;

  switch (syscall_kind) {
    case SyscallKind::kChannelRead:
      message = output_event->GetMessage();
      break;
    case SyscallKind::kChannelWrite:
    case SyscallKind::kChannelCall:
      message = output_event->invoked_event()->GetMessage();
      break;
    default:
      return nullptr;
      break;
  }

  if (message->method() == nullptr) {
    // TODO(nimaj): investigate why this happens for zx_channel_read and zx_channel_write
    // We will not be able to determine method name nor interface name
    return nullptr;
  }

  // Extract handle information from output event in 2 steps:
  // (1/2) Find handle's struct member
  const fidl_codec::StructMember* handle_member = syscall->SearchInlineMember("handle", true);
  // (2/2) Lookup handle's struct member in invoked_event
  const fidl_codec::HandleValue* handle =
      output_event->invoked_event()->GetHandleValue(handle_member);
  zx_handle_t handle_id = handle->handle().handle;

  bool crashed = output_event->returned_value() == ZX_ERR_PEER_CLOSED;

  return std::make_unique<FidlCallInfo>(crashed, message->method()->enclosing_interface().name(),
                                        handle_id, syscall_kind, message->method()->name());
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
