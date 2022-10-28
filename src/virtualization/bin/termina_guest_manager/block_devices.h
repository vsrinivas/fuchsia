// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_BLOCK_DEVICES_H_
#define SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_BLOCK_DEVICES_H_

#include <fuchsia/hardware/block/volume/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/zx/result.h>
#include <zircon/hw/gpt.h>

#include <vector>

#include "src/virtualization/bin/termina_guest_manager/termina_config.h"

constexpr const char kGuestPartitionName[] = "guest";

constexpr std::array<uint8_t, fuchsia::hardware::block::partition::GUID_LENGTH>
    kGuestPartitionGuid = {
        0x9a, 0x17, 0x7d, 0x2d, 0x8b, 0x24, 0x4a, 0x4c,
        0x87, 0x11, 0x1f, 0x99, 0x05, 0xb7, 0x6e, 0xd1,
};

fit::result<std::string, std::vector<fuchsia::virtualization::BlockSpec>> GetBlockDevices(
    const termina_config::Config& structured_config);

void DropDevNamespace();

enum class VolumeAction {
  KEEP,
  REMOVE,
};
zx::result<> WipeStatefulPartition(size_t bytes_to_zero, uint8_t value = 0,
                                   VolumeAction = VolumeAction::REMOVE);

#endif  // SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_BLOCK_DEVICES_H_
