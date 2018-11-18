// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parent.h"

#include <fcntl.h>
#include <string.h>

#include <fs-management/ram-nand.h>
#include <zircon/assert.h>
#include <zircon/device/device.h>

ParentDevice::ParentDevice(const TestConfig& config) : config_(config) {
    if (config_.path) {
        device_.reset(open(config_.path, O_RDWR));
        strncpy(path_, config_.path, sizeof(path_) - 1);
    } else {
        zircon_nand_RamNandInfo ram_nand_config = {};
        ram_nand_config.nand_info = config_.info;
        ram_nand_config.vmo = ZX_HANDLE_INVALID;
        if (config_.partition_map.partition_count > 0) {
            ram_nand_config.partition_map = config_.partition_map;
            ram_nand_config.export_nand_config = true;
            ram_nand_config.export_partition_map = true;
        } else {
            ram_nand_config.export_partition_map = false;
        }
        if (create_ram_nand(&ram_nand_config, path_) == ZX_OK) {
            ram_nand_.reset(open(path_, O_RDWR));
            config_.num_blocks = config.info.num_blocks;
        }
    }
}

ParentDevice::~ParentDevice() {
    if (ram_nand_) {
        ioctl_device_unbind(ram_nand_.get());
    }
}

void ParentDevice::SetInfo(const zircon_nand_Info& info) {
    ZX_DEBUG_ASSERT(!ram_nand_);
    config_.info = info;
    if (!config_.num_blocks) {
        config_.num_blocks = info.num_blocks;
    }
}
