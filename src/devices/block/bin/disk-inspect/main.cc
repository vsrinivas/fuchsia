// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <lib/cmdline/args_parser.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <zircon/status.h>

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <disk_inspector/command.h>
#include <disk_inspector/command_handler.h>
#include <disk_inspector/disk_inspector.h>
#include <disk_inspector/inspector_transaction_handler.h>
#include <disk_inspector/vmo_buffer_factory.h>
#include <fbl/unique_fd.h>

#include "src/lib/line_input/modal_line_input.h"
#include "src/storage/minfs/command_handler.h"
#include "src/storage/minfs/minfs_inspector.h"

namespace {

bool should_quit = false;

constexpr char kHelpIntro[] =
    R"(Tool for inspecting a block device as a filesystem. Typical usage:

  disk-inspect --device /dev/class/block/002 --name minfs

Options

)";

const char kDeviceHelp[] =
    "  --device (-d) <device-name>\n"
    "      The path to the block device to use. For example,\n"
    "      \"/dev/class/block/000\".";
const char kHelpHelp[] =
    "  --help (-h)\n"
    "      Prints usage instructions.";
const char kNameHelp[] =
    "  --name (-n) <format>\n"
    "      The filesystem type of the block device. Only \"minfs\" is currently\n"
    "      supported.";

// Configuration info (what to do).
struct Config {
  std::string path;
  std::string name;
};

std::optional<Config> GetOptions(int argc, char** argv) {
  cmdline::ArgsParser<Config> parser;

  bool print_help = argc == 1;  // Default to showing help if nothing is specified.
  parser.AddSwitch("device", 'd', kDeviceHelp, &Config::path);
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&print_help]() { print_help = true; });
  parser.AddSwitch("name", 'n', kNameHelp, &Config::name);

  std::vector<std::string> params;
  Config config;
  if (auto status = parser.Parse(argc, argv, &config, &params); status.has_error()) {
    std::cerr << status.error_message() << "\n\n";
    return std::nullopt;
  }

  // Check for explicitly-requested help.
  if (print_help) {
    std::cout << kHelpIntro << parser.GetHelp();
    return std::nullopt;
  }

  // There should be no non-switch args.
  if (!params.empty()) {
    std::cerr << "This program takes no non-switch arguments. See --help.\n\n";
    return std::nullopt;
  }

  // Validate.
  if (config.path.empty() || config.name.empty()) {
    std::cerr << "Both --device and --name are required.\n\n";
    return std::nullopt;
  }

  return config;
}

fit::result<uint32_t, std::string> GetBlockSize(const std::string& name) {
  if (name == "minfs") {
    return fit::ok(minfs::kMinfsBlockSize);
  }
  return fit::error("FS with label \"" + name +
                    "\" is not supported for inspection.\nSupported types: minfs\n");
}

std::unique_ptr<disk_inspector::CommandHandler> GetHandler(const char* path, const char* fs_name) {
  fbl::unique_fd fd(open(path, O_RDONLY));

  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    std::cerr << "Cannot acquire handle with error: " << status << "\n";
    return nullptr;
  }

  std::string name(fs_name);
  auto size_result = GetBlockSize(name);

  if (size_result.is_error()) {
    std::cerr << size_result.take_error();
    return nullptr;
  }

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  status = block_client::RemoteBlockDevice::Create(std::move(channel), &device);
  if (status != ZX_OK) {
    std::cerr << "Cannot create remote device: " << status << "\n";
    return nullptr;
  }

  uint32_t block_size = size_result.take_value();
  std::unique_ptr<disk_inspector::InspectorTransactionHandler> inspector_handler;
  status = disk_inspector::InspectorTransactionHandler::Create(std::move(device), block_size,
                                                               &inspector_handler);
  if (status != ZX_OK) {
    std::cerr << "Cannot create TransactionHandler.\n";
    return nullptr;
  }
  auto buffer_factory =
      std::make_unique<disk_inspector::VmoBufferFactory>(inspector_handler.get(), block_size);

  std::unique_ptr<disk_inspector::CommandHandler> handler;

  if (name == "minfs") {
    auto result =
        minfs::MinfsInspector::Create(std::move(inspector_handler), std::move(buffer_factory));
    if (result.is_error()) {
      return nullptr;
    }
    handler = std::make_unique<minfs::CommandHandler>(result.take_value());
  }

  return handler;
}

void OnLineTyped(line_input::ModalLineInputStdout& input, disk_inspector::CommandHandler* handler,
                 const std::string& line) {
  if (line.find_first_not_of(' ') == std::string::npos)
    return;

  input.AddToHistory(line);

  // Hide the input line so output gets appended without the lines of typing.
  input.Hide();

  std::stringstream ss(line);
  std::istream_iterator<std::string> begin(ss);
  std::istream_iterator<std::string> end;
  std::vector<std::string> command_args(begin, end);
  if (command_args[0] == "exit" || command_args[0] == "quit" || command_args[0] == "q") {
    should_quit = true;
    return;  // Don't unhide when exiting.
  }

  if (command_args[0] == "help" || command_args[0] == "h" || command_args[0] == "?") {
    handler->PrintSupportedCommands();
  } else {
    if (zx_status_t status = handler->CallCommand(command_args); status != ZX_OK) {
      switch (status) {
        case ZX_ERR_NOT_SUPPORTED:
          std::cerr << "Command not supported.\n";
          break;
        default:
          std::cerr << "Call command failed with error: " << zx_status_get_string(status) << " ("
                    << status << ")\n";
          break;
      }
    }
  }

  // Re-show to match Hide() call at the top.
  input.Show();
}

}  //  namespace

int main(int argc, char** argv) {
  auto option_result = GetOptions(argc, argv);
  if (!option_result)
    return -1;

  const Config& config = *option_result;
  std::unique_ptr<disk_inspector::CommandHandler> handler =
      GetHandler(config.path.c_str(), config.name.c_str());
  if (!handler) {
    std::cerr << "Could not get inspector at path. Closing.\n";
    return -1;
  }

  std::cout << "Starting " << config.name
            << " inspector. Type \"help\" to get available commands.\n";
  std::cout << "Type \"exit\" or \"quit\" to quit the application.\n";

  line_input::ModalLineInputStdout input;
  input.Init(
      [&input, &handler](const std::string& line) { OnLineTyped(input, handler.get(), line); },
      "[disk-inspect] ");

  input.Show();
  while (!should_quit)
    input.OnInput(static_cast<char>(getc(stdin)));

  return 0;
}
