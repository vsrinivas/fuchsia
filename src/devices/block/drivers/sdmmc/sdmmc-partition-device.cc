// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-partition-device.h"

#include <string.h>
#include <zircon/hw/gpt.h>

#include "sdmmc-block-device.h"
#include "sdmmc-types.h"

namespace sdmmc {

zx_status_t PartitionDevice::AddDevice() {
  switch (partition_) {
    case USER_DATA_PARTITION:
      return DdkAdd("user");
    case BOOT_PARTITION_1:
      return DdkAdd("boot1");
    case BOOT_PARTITION_2:
      return DdkAdd("boot2");
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_off_t PartitionDevice::DdkGetSize() { return block_info_.block_count * block_info_.block_size; }

zx_status_t PartitionDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = reinterpret_cast<ddk::AnyProtocol*>(out);
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
      proto->ops = &block_impl_protocol_ops_;
      proto->ctx = this;
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
      if (partition_ == USER_DATA_PARTITION) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      proto->ops = &block_partition_protocol_ops_;
      proto->ctx = this;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void PartitionDevice::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  memcpy(info_out, &block_info_, sizeof(*info_out));
  *block_op_size_out = BlockOperation::OperationSize(sizeof(block_op_t));
}

void PartitionDevice::BlockImplQueue(block_op_t* btxn, block_impl_queue_callback completion_cb,
                                     void* cookie) {
  BlockOperation txn(btxn, completion_cb, cookie, sizeof(block_op_t));
  txn.private_storage()->partition = partition_;
  txn.private_storage()->block_count = block_info_.block_count;
  sdmmc_parent_->Queue(std::move(txn));
}

zx_status_t PartitionDevice::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
  ZX_DEBUG_ASSERT(partition_ != USER_DATA_PARTITION);

  constexpr uint8_t kGuidEmmcBoot1Value[] = GUID_EMMC_BOOT1_VALUE;
  constexpr uint8_t kGuidEmmcBoot2Value[] = GUID_EMMC_BOOT2_VALUE;

  switch (guid_type) {
    case GUIDTYPE_TYPE:
      if (partition_ == BOOT_PARTITION_1) {
        memcpy(&out_guid->data1, kGuidEmmcBoot1Value, GUID_LENGTH);
      } else {
        memcpy(&out_guid->data1, kGuidEmmcBoot2Value, GUID_LENGTH);
      }
      return ZX_OK;
    case GUIDTYPE_INSTANCE:
      return ZX_ERR_NOT_SUPPORTED;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t PartitionDevice::BlockPartitionGetName(char* out_name, size_t capacity) {
  ZX_DEBUG_ASSERT(partition_ != USER_DATA_PARTITION);
  if (capacity <= strlen(name())) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  strlcpy(out_name, name(), capacity);

  return ZX_OK;
}

}  // namespace sdmmc
