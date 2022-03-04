// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_PARTITION_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_PARTITION_DEVICE_H_

#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <zircon/types.h>

#include <cinttypes>

#include <ddktl/device.h>

#include "sdmmc-types.h"

namespace sdmmc {

class SdmmcBlockDevice;

class PartitionDevice;
using PartitionDeviceType = ddk::Device<PartitionDevice, ddk::GetSizable, ddk::GetProtocolable>;

class PartitionDevice : public PartitionDeviceType,
                        public ddk::BlockImplProtocol<PartitionDevice, ddk::base_protocol>,
                        public ddk::BlockPartitionProtocol<PartitionDevice> {
 public:
  PartitionDevice(zx_device_t* parent, SdmmcBlockDevice* sdmmc_parent,
                  const block_info_t& block_info, EmmcPartition partition)
      : PartitionDeviceType(parent),
        sdmmc_parent_(sdmmc_parent),
        block_info_(block_info),
        partition_(partition) {}

  zx_status_t AddDevice();

  void DdkRelease() { delete this; }

  zx_off_t DdkGetSize();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
  void BlockImplQueue(block_op_t* btxn, block_impl_queue_callback completion_cb, void* cookie);

  zx_status_t BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid);
  zx_status_t BlockPartitionGetName(char* out_name, size_t capacity);

 private:
  SdmmcBlockDevice* const sdmmc_parent_;
  const block_info_t block_info_;
  const EmmcPartition partition_;
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_PARTITION_DEVICE_H_
