// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parent.h"

#include <fcntl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/component/cpp/service_client.h>
#include <string.h>
#include <zircon/assert.h>

#include <variant>

#include <fbl/unique_fd.h>

ParentDevice::ParentDevice(fidl::ClientEnd<fuchsia_device::Controller> controller,
                           const TestConfig& config)
    : device_(std::move(controller)), config_(config) {}

ParentDevice::ParentDevice(ramdevice_client::RamNand ram_nand, const TestConfig& config)
    : device_(std::move(ram_nand)), config_(config) {}

zx::result<ParentDevice> ParentDevice::Create(const TestConfig& config) {
  if (config.path) {
    zx::result controller = component::Connect<fuchsia_device::Controller>(config.path);
    if (controller.is_error()) {
      return controller.take_error();
    }
    return zx::ok(ParentDevice(std::move(controller.value()), config));
  }
  fuchsia_hardware_nand::wire::RamNandInfo ram_nand_config = {
      .nand_info = config.info,
  };
  if (config.partition_map.partition_count != 0) {
    ram_nand_config.partition_map = config.partition_map;
    ram_nand_config.export_nand_config = true;
    ram_nand_config.export_partition_map = true;
  } else {
    ram_nand_config.export_partition_map = false;
  }
  std::optional<ramdevice_client::RamNand> ram_nand;
  if (zx_status_t status = ramdevice_client::RamNand::Create(std::move(ram_nand_config), &ram_nand);
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(ParentDevice(std::move(ram_nand.value()), [config = config]() mutable {
    config.num_blocks = config.info.num_blocks;
    return config;
  }()));
}

const char* ParentDevice::Path() const {
  return std::visit(
      [this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, fidl::ClientEnd<fuchsia_device::Controller>>) {
          return config_.path;
        } else if constexpr (std::is_same_v<T, ramdevice_client::RamNand>) {
          return arg.path();
        }
      },
      device_);
}

const fidl::ClientEnd<fuchsia_device::Controller>& ParentDevice::controller() const {
  return std::visit(
      [](auto&& arg) -> const fidl::ClientEnd<fuchsia_device::Controller>& {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, fidl::ClientEnd<fuchsia_device::Controller>>) {
          return arg;
        } else if constexpr (std::is_same_v<T, ramdevice_client::RamNand>) {
          return arg.controller();
        }
      },
      device_);
}

void ParentDevice::SetInfo(const fuchsia_hardware_nand::wire::Info& info) {
  ZX_DEBUG_ASSERT(!std::holds_alternative<ramdevice_client::RamNand>(device_));
  config_.info = info;
  if (!config_.num_blocks) {
    config_.num_blocks = info.num_blocks;
  }
}
