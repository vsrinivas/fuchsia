// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <getopt.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <zircon/status.h>

#include <iostream>
#include <memory>
#include <sstream>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <disk_inspector/command.h>
#include <disk_inspector/command_handler.h>
#include <disk_inspector/disk_inspector.h>
#include <disk_inspector/inspector_transaction_handler.h>
#include <disk_inspector/vmo_buffer_factory.h>
#include <fbl/unique_fd.h>
#include <minfs/command_handler.h>
#include <minfs/minfs_inspector.h>

namespace {

constexpr char kUsageMessage[] = R"""(
Tool for inspecting a block device as a filesystem.

disk-inspect --device /dev/class/block/002 --name minfs

Options:
  --device (-d) path : Specifies the block device to use.
  --name (-n) : What filesystem type to represent the block device. Only
                supports "minfs" for now.
)""";

// Configuration info (what to do).
struct Config {
  const char* path;
  const char* name;
};

bool GetOptions(int argc, char** argv, Config* config) {
  while (true) {
    struct option options[] = {
        {"device", required_argument, nullptr, 'd'},
        {"name", required_argument, nullptr, 'n'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "d:n:h", options, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'd':
        config->path = optarg;
        break;
      case 'n':
        config->name = optarg;
        break;
      case 'h':
        std::cout << kUsageMessage << "\n";
        return false;
    }
  }
  return argc == optind;
}

bool ValidateOptions(const Config& config) { return !(!config.path || !config.name); }

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

}  //  namespace

int main(int argc, char** argv) {
  Config config = {};
  if (!GetOptions(argc, argv, &config)) {
    std::cout << kUsageMessage << "\n";
    return -1;
  }

  if (!ValidateOptions(config)) {
    std::cout << kUsageMessage << "\n";
    return -1;
  }

  std::unique_ptr<disk_inspector::CommandHandler> handler = GetHandler(config.path, config.name);
  if (handler == nullptr) {
    std::cerr << "Could not get inspector at path. Closing.\n";
    return -1;
  }

  std::cout << "Starting " << config.name
            << " inspector. Type \"help\" to get available commands.\n";
  std::cout << "Type \"exit\" to quit the application.\n";
  while (true) {
    std::string command_str;
    getline(std::cin, command_str);
    if (command_str.find_first_not_of(' ') == std::string::npos) {
      continue;
    }
    std::stringstream ss(command_str);
    std::istream_iterator<std::string> begin(ss);
    std::istream_iterator<std::string> end;
    std::vector<std::string> command_args(begin, end);
    if (command_args[0] == "exit") {
      return 0;
    }
    if (command_args[0] == "help") {
      handler->PrintSupportedCommands();
      continue;
    }
    zx_status_t status = handler->CallCommand(command_args);
    if (status != ZX_OK) {
      switch (status) {
        case ZX_ERR_NOT_SUPPORTED: {
          std::cerr << "Command not supported.\n";
          break;
        }
        default: {
          std::cerr << "Call command failed with error: " << status << "\n";
        }
      }
    }
  }
  return 0;
}
