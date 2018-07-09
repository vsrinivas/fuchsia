// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <ddktl/device.h>
#include <ddktl/protocol/nand.h>
#include <fbl/macros.h>
#include <zircon/types.h>

#include "ram-nand.h"

class RamNandDevice;
using DeviceType = ddk::Device<RamNandDevice, ddk::GetSizable, ddk::Unbindable, ddk::Ioctlable>;

class RamNandDevice : public DeviceType, public ddk::NandProtocol<RamNandDevice> {
  public:
    explicit RamNandDevice(zx_device_t* parent, const NandParams& params)
        : DeviceType(parent), ram_nand_(params) {}

    zx_status_t Bind();
    void DdkRelease() { delete this; }

    // Device protocol implementation.
    zx_off_t DdkGetSize();
    void DdkUnbind();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

    // NAND protocol implementation.
    void Query(nand_info_t* info_out, size_t* nand_op_size_out);
    void Queue(nand_op_t* operation);
    zx_status_t GetFactoryBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                                       uint32_t* num_bad_blocks);

  private:
    NandDevice ram_nand_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(RamNandDevice);
};
