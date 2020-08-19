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
        AddFidlHeaderForInterface(call_info->enclosing_interface_name());
        AddEventToLog(std::move(call_info));
      }
    }
  }

  std::cout << "Writing tests on disk\n"
            << "  process name: " << dispatcher_->processes().begin()->second->name() << "\n"
            << "  output directory: " << output_directory_ << "\n";

  for (const auto& [handle_id, calls] : call_log()) {
    std::string protocol_name;

    for (const auto& call_info : calls) {
      if (protocol_name.empty()) {
        protocol_name = call_info->enclosing_interface_name();
      }

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

    WriteTestToFile(protocol_name);
    std::cout << "\n";
  }
}

void TestGenerator::WriteTestToFile(std::string_view protocol_name) {
  std::filesystem::path file_name =
      output_directory_ /
      std::filesystem::path(ToSnakeCase(protocol_name) + "_" +
                            std::to_string(test_counter_[std::string(protocol_name)]++) + ".cc");
  std::cout << "... Writing to " << file_name << "\n";

  std::ofstream target_file;
  target_file.open(file_name, std::ofstream::out);

  GenerateIncludes(target_file);

  target_file << "TEST(" << ToSnakeCase(dispatcher_->processes().begin()->second->name()) << ", "
              << ToSnakeCase(protocol_name) << ") {\n";
  target_file << "  Proxy proxy;\n";
  target_file << "  proxy.run();\n";
  target_file << "}\n";

  target_file.close();
}

}  // namespace fidlcat
