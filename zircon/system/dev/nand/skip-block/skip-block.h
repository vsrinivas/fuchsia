// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <ddktl/device.h>
#include <ddktl/protocol/badblock.h>
#include <ddktl/protocol/nand.h>
#include <ddktl/protocol/empty-protocol.h>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <lib/operation/nand.h>
#include <fuchsia/hardware/skipblock/c/fidl.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "logical-to-physical-map.h"

namespace nand {

using NandOperation = nand::Operation<>;

using PartitionInfo = fuchsia_hardware_skipblock_PartitionInfo;
using ReadWriteOperation = fuchsia_hardware_skipblock_ReadWriteOperation;

class SkipBlockDevice;
using DeviceType = ddk::Device<SkipBlockDevice, ddk::GetSizable, ddk::Unbindable,
                               ddk::Messageable>;

class SkipBlockDevice : public DeviceType,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_SKIP_BLOCK> {
public:
    // Spawns device node based on parent node.
    static zx_status_t Create(zx_device_t* parent);

    zx_status_t Bind();

    // Device protocol implementation.
    zx_off_t DdkGetSize();
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

    // skip-block fidl implementation.
    zx_status_t GetPartitionInfo(PartitionInfo* info);
    zx_status_t Read(const ReadWriteOperation& op);
    zx_status_t Write(const ReadWriteOperation& op, bool* bad_block_grown);

private:
    explicit SkipBlockDevice(zx_device_t* parent, ddk::NandProtocolClient nand,
                             ddk::BadBlockProtocolClient bad_block, uint32_t copy_count)
        : DeviceType(parent), nand_(nand), bad_block_(bad_block), copy_count_(copy_count) {
        nand_.Query(&nand_info_, &parent_op_size_);
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(SkipBlockDevice);

    uint64_t GetBlockSize() const { return nand_info_.pages_per_block * nand_info_.page_size; }

    // Helper to get bad block list in a more idiomatic container.
    zx_status_t GetBadBlockList(fbl::Array<uint32_t>* bad_block_list) TA_REQ(lock_);
    // Helper to validate VMO received through IOCTL.
    zx_status_t ValidateVmo(const ReadWriteOperation& op) const;

    ddk::NandProtocolClient nand_ __TA_GUARDED(lock_);
    ddk::BadBlockProtocolClient bad_block_ __TA_GUARDED(lock_);
    LogicalToPhysicalMap block_map_ __TA_GUARDED(lock_);
    fbl::Mutex lock_;
    fuchsia_hardware_nand_Info nand_info_;
    size_t parent_op_size_;

    std::optional<NandOperation> nand_op_ __TA_GUARDED(lock_);

    const uint32_t copy_count_;
};

} // namespace nand
