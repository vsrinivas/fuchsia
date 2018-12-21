// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <ddk/protocol/badblock.h>
#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/badblock.h>
#include <fbl/macros.h>
#include <lib/ftl/volume.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

namespace ftl {

struct BlockParams {
    uint64_t GetSize() const {
        return static_cast<uint64_t>(page_size) * num_pages;
    }

    uint32_t page_size;
    uint32_t num_pages;
};

class BlockDevice;
using DeviceType = ddk::Device<BlockDevice, ddk::GetSizable, ddk::Unbindable>;

// Provides the bulk of the functionality for a FTL-backed block device.
class BlockDevice : public DeviceType, public ftl::FtlInstance  {
  public:
    explicit BlockDevice(zx_device_t* parent = nullptr) : DeviceType(parent) {}
    ~BlockDevice();

    zx_status_t Bind();
    void DdkRelease() { delete this; }
    void DdkUnbind();

    // Performs the object initialization.
    zx_status_t Init();

    // Device protocol implementation.
    zx_off_t DdkGetSize() { return params_.GetSize(); }

    // FtlInstance interface.
    bool OnVolumeAdded(uint32_t page_size, uint32_t num_pages) final;

    void SetVolumeForTest(std::unique_ptr<ftl::Volume> volume) {
        volume_ = std::move(volume);
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(BlockDevice);

  private:
    bool InitFtl();

    BlockParams params_ = {};

    nand_protocol_t parent_ = {};
    bad_block_protocol_t bad_block_ = {};
    std::unique_ptr<ftl::Volume> volume_;
    uint8_t guid_[ZBI_PARTITION_GUID_LEN] = {};
};

}  // namespace ftl.
