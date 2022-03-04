// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_TYPES_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_TYPES_H_

#include <fidl/fuchsia.hardware.rpmb/cpp/wire.h>
#include <lib/operation/block.h>

#include <cinttypes>

namespace sdmmc {

// See the eMMC specification section 7.4.69 for these constants.
enum EmmcPartition : uint8_t {
  USER_DATA_PARTITION = 0x0,
  BOOT_PARTITION_1 = 0x1,
  BOOT_PARTITION_2 = 0x2,
  RPMB_PARTITION = 0x3,
  PARTITION_COUNT,
};

struct PartitionInfo {
  enum EmmcPartition partition;
  uint64_t block_count;
};

struct RpmbRequestInfo {
  fuchsia_mem::wire::Range tx_frames = {};
  fuchsia_mem::wire::Range rx_frames = {};
  fidl::WireServer<fuchsia_hardware_rpmb::Rpmb>::RequestCompleter::Async completer;
};

using BlockOperation = block::BorrowedOperation<PartitionInfo>;

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_TYPES_H_
