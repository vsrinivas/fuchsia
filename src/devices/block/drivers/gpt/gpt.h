// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_GPT_GPT_H_
#define SRC_STORAGE_BLOCK_DRIVERS_GPT_GPT_H_

#include <lib/sync/completion.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/block/partition.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <ddktl/protocol/block/partition.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <gpt/c/gpt.h>
#include <gpt/gpt.h>

namespace gpt {

class PartitionTable;
class PartitionDevice;
using TableRef = fbl::RefPtr<PartitionTable>;
using DeviceType =
    ddk::Device<PartitionDevice, ddk::GetProtocolable, ddk::GetSizable, ddk::Unbindable>;

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

  // Schedule device for unbind and release.
  void AsyncRemove();

  // Block protocol implementation.
  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
  void BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb, void* cookie);

  // Device protocol.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_off_t DdkGetSize();
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

class PartitionTable : public fbl::RefCounted<PartitionTable> {
 public:
  explicit PartitionTable(zx_device_t* parent) : parent_(parent) {}

  // Device bind() interface.
  // Bind call creates Table and scans partitions.
  static zx_status_t CreateAndBind(void* ctx, zx_device_t* parent);

  // Breakout of CreateAndBind suitable for testing.
  static zx_status_t Create(zx_device_t* parent, TableRef* out,
                            fbl::Vector<std::unique_ptr<PartitionDevice>>* devices = nullptr);
  zx_status_t Bind();

 private:
  zx_device_t* parent_ = nullptr;
  uint64_t guid_map_entries_ = 0;
  guid_map_t guid_map_[DEVICE_METADATA_GUID_MAP_MAX_ENTRIES]{};
  // Used by tests to retrieve device list. Not managed by this class.
  fbl::Vector<std::unique_ptr<PartitionDevice>>* devices_ = nullptr;
};

}  // namespace gpt

#endif  // SRC_STORAGE_BLOCK_DRIVERS_GPT_GPT_H_
