// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "usb-mass-storage.h"
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <stdint.h>

namespace ums {

class UsbMassStorageDevice;

struct BlockDeviceParameters {
    uint64_t total_blocks;

    uint32_t block_size;

    uint8_t lun; // our logical unit number

    uint32_t flags; // flags for block_info_t

    uint32_t max_transfer;

    bool device_added;

    bool cache_enabled;
};

class UmsBlockDevice;
using DeviceType = ddk::Device<UmsBlockDevice, ddk::GetSizable, ddk::Unbindable>;
class UmsBlockDevice : public DeviceType,
                       public ddk::BlockImplProtocol<UmsBlockDevice, ddk::base_protocol>,
                       public fbl::RefCounted<UmsBlockDevice> {
public:
    explicit UmsBlockDevice(zx_device_t* parent, uint8_t lun,
                            fbl::Function<void(ums::Transaction*)>&& queue_callback)
        : DeviceType(parent), queue_callback_(std::move(queue_callback)) {
        parameters_ = {};
        parameters_.lun = lun;
    }

    zx_status_t Add();

    // Device protocol implementation.
    zx_off_t DdkGetSize();

    void DdkRelease();

    // Block protocol implementation.
    void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);

    void BlockImplQueue(block_op_t* op, block_impl_queue_callback completion_cb,
                        void* cookie);

    void DdkUnbind(){}

    const BlockDeviceParameters& GetBlockDeviceParameters() {
        return parameters_;
    }

    void SetBlockDeviceParameters(const BlockDeviceParameters& parameters) {
        parameters_ = parameters;
    }

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(UmsBlockDevice);

private:
    fbl::Function<void(ums::Transaction*)> queue_callback_;
    BlockDeviceParameters parameters_;
};
} // namespace ums
