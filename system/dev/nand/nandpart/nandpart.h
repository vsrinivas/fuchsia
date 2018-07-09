// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/nand.h>

#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <zircon/types.h>

namespace nand {

class NandPartDevice;
using DeviceType = ddk::Device<NandPartDevice, ddk::GetSizable>;

class NandPartDevice : public DeviceType,
                       public ddk::NandProtocol<NandPartDevice> {
public:
    // Spawns device nodes based on parent node.
    static zx_status_t Create(zx_device_t* parent);

    zx_status_t Bind(const char* name);

    // Device protocol implementation.
    zx_off_t DdkGetSize() {
        //TODO: use query() results, *but* fvm returns different query and getsize
        // results, and the latter are dynamic...
        return device_get_size(parent());
    }
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* protocol);
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

    // nand protocol implementation.
    void Query(nand_info_t* info_out, size_t* nand_op_size_out);
    void Queue(nand_op_t* op);
    zx_status_t GetFactoryBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                                       uint32_t* num_bad_blocks);

private:
    explicit NandPartDevice(zx_device_t* parent, const nand_protocol_t& nand_proto,
                            size_t parent_op_size,
                            const nand_info_t& nand_info, uint32_t erase_block_start)
        : DeviceType(parent), nand_proto_(nand_proto), nand_(&nand_proto_),
          parent_op_size_(parent_op_size), nand_info_(nand_info),
          erase_block_start_(erase_block_start) {}

    DISALLOW_COPY_ASSIGN_AND_MOVE(NandPartDevice);

    nand_protocol_t nand_proto_;
    ddk::NandProtocolProxy nand_;

    // op_size for parent device.
    size_t parent_op_size_;
    // info about nand.
    nand_info_t nand_info_;
    // First erase block for the partition.
    uint32_t erase_block_start_;
};

} // namespace nand
