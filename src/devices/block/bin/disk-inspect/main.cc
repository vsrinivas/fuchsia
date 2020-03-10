// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <zircon/status.h>

#include <iostream>
#include <sstream>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <disk_inspector/command.h>
#include <disk_inspector/command_handler.h>
#include <disk_inspector/disk_inspector.h>
#include <fbl/unique_fd.h>
#include <minfs/command-handler.h>

namespace {

namespace fuchsia_partition = llcpp::fuchsia::hardware::block::partition;

std::unique_ptr<disk_inspector::CommandHandler> GetHandler(const char* path) {
  fbl::unique_fd fd(open(path, O_RDONLY));

  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    std::cerr << "Cannot acquire handle with error: " << status << "\n";
    return nullptr;
  }

  auto name_resp = fuchsia_partition::Partition::Call::GetName(channel.borrow());
  if (!name_resp.ok()) {
    std::cerr << "Get block device name failed.\n";
    return nullptr;
  }
  if (name_resp->status != ZX_OK) {
    std::cerr << "Could not get block device fs name: " << name_resp->status << "\n";
    return nullptr;
  }
  if (name_resp->name.is_null()) {
    std::cerr << "Block device name is null.\n";
    return nullptr;
  }
  std::string name(name_resp->name.begin(), name_resp->name.size());
  std::cout << "Found block partition label: " << name << "\n";

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  status = block_client::RemoteBlockDevice::Create(std::move(channel), &device);
  if (status != ZX_OK) {
    std::cerr << "Cannot create remote device: " << status << "\n";
    return nullptr;
  }

  std::unique_ptr<disk_inspector::CommandHandler> handler;

  if (name == "minfs") {
    status = minfs::CommandHandler::Create(std::move(device), &handler);
  } else {
    std::cerr << "FS with label " << name << " is not supported for inspection.\n";
    return nullptr;
  }

  if (status != ZX_OK) {
    std::cerr << "Could not create inspector for FS with error: " << status << "\n";
    return nullptr;
  }
  return handler;
}

}  //  namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <device path>\n";
    return -1;
  }

  std::unique_ptr<disk_inspector::CommandHandler> handler = GetHandler(argv[1]);
  if (handler == nullptr) {
    std::cerr << "Could not get inspector at path. Closing.\n";
    return -1;
  }

  std::cout << "Starting inspector. Type \"help\" to get available commands.\n";
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
