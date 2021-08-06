// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_GPT_GPT_H_
#define SRC_DEVICES_BLOCK_DRIVERS_GPT_GPT_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/c/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/sync/completion.h>
#include <zircon/types.h>

#include <ddk/metadata/gpt.h>
#include <ddktl/device.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <gpt/c/gpt.h>
#include <gpt/gpt.h>

namespace gpt {

class PartitionDevice;
using DeviceType = ddk::Device<PartitionDevice, ddk::GetProtocolable, ddk::GetSizable>;

class PartitionDevice : public DeviceType,
                        public ddk::BlockImplProtocol<PartitionDevice, ddk::base_protocol>,
                        public ddk::BlockPartitionProtocol<PartitionDevice> {
 public:
  PartitionDevice(zx_device_t* parent, block_impl_protocol_t* proto) : DeviceType(parent) {
    memcpy(&block_protocol_, proto, sizeof(block_protocol_));
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(PartitionDevice);

  void SetInfo(gpt_entry_t* entry, block_info_t* info, size_t op_size);

  // Add device to devhost device list. Once added, the device cannot be deleted directly,
  // AsyncRemove() must be called to schedule an Unbind() and Release().
  zx_status_t Add(uint32_t partition_number, uint32_t flags = 0);

  // Block protocol implementation.
  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
  void BlockImplQueue(block_op_t* bop, block_impl_queue_callback completion_cb, void* cookie);

  // Device protocol.
  void DdkRelease();
  zx_off_t DdkGetSize() const;
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  // Partition protocol implementation.
  zx_status_t BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid);
  zx_status_t BlockPartitionGetName(char* out_name, size_t capacity);

 private:
  size_t block_op_size_ = 0;
  block_impl_protocol_t block_protocol_{};
  gpt_entry_t gpt_entry_{};
  block_info_t info_{};
};

// Device bind() interface.
// Bind call creates Table and scans partitions.
zx_status_t Bind(void* ctx, zx_device_t* parent);

}  // namespace gpt

#endif  // SRC_DEVICES_BLOCK_DRIVERS_GPT_GPT_H_
