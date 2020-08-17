#include "tools/fidlcat/lib/code_generator/test_generator.h"

namespace fidlcat {

void TestGenerator::GenerateTests() {
  if (dispatcher_->processes().size() != 1) {
    std::cout << "Error: Cannot generate tests for more than one process.\n";
    return;
  }

  for (const auto event : dispatcher_->decoded_events()) {
    OutputEvent* output_event = event->AsOutputEvent();
    if (output_event) {
      auto call_info = OutputEventToFidlCallInfo(output_event);
      if (call_info) {
        AddEventToLog(std::move(call_info));
      }
    }
  }

  std::cout << "Writing tests on disk"
            << " (session id: " << session_id_
            << ", process name: " << dispatcher_->processes().begin()->second->name() << ")\n";
  for (const auto& [handle_id, calls] : call_log()) {
    for (const auto& call_info : calls) {
      std::cout << call_info->handle_id() << " ";
      switch (call_info->kind()) {
        case SyscallKind::kChannelWrite:
          std::cout << "zx_channel_write";
          break;
        case SyscallKind::kChannelRead:
          std::cout << "zx_channel_read";
          break;
        case SyscallKind::kChannelCall:
          std::cout << "zx_channel_call";
          break;
        default:
          break;
      }
      if (call_info->crashed()) {
        std::cout << " (crashed)";
      }
      std::cout << " " << call_info->enclosing_interface_name() << "." << call_info->method_name()
                << "\n";
    }
  }
}

}  // namespace fidlcat
