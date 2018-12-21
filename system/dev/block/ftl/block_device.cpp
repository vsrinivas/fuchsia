// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_device.h"

#include <ddk/debug.h>
#include <zircon/assert.h>

#include "nand_driver.h"

namespace ftl {

BlockDevice::~BlockDevice() {
    bool volume_created = (DdkGetSize() != 0);
    if (volume_created) {
        if (volume_->Unmount() != ZX_OK) {
            zxlogf(ERROR, "FTL: FtlUmmount() failed");
        }
    }
}

zx_status_t BlockDevice::Bind() {
    zxlogf(INFO, "FTL: parent: '%s'\n", device_get_name(parent()));

    if (device_get_protocol(parent(), ZX_PROTOCOL_NAND, &parent_) != ZX_OK) {
        zxlogf(ERROR, "FTL: device '%s' does not support nand protocol\n",
               device_get_name(parent()));
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Get the optional bad block protocol.
    if (device_get_protocol(parent(), ZX_PROTOCOL_BAD_BLOCK, &bad_block_) != ZX_OK) {
        zxlogf(WARN, "FTL: Parent device '%s': does not support bad_block protocol\n",
               device_get_name(parent()));
    }

    zx_status_t status = Init();
    if (status != ZX_OK) {
        return status;
    }
    return DdkAdd("ftl");
}

void BlockDevice::DdkUnbind() {
    DdkRemove();
}

zx_status_t BlockDevice::Init() {
    if (!InitFtl()) {
        return ZX_ERR_NO_RESOURCES;
    }

    return ZX_OK;
}

bool BlockDevice::OnVolumeAdded(uint32_t page_size, uint32_t num_pages) {
    params_ = {page_size, num_pages};
    zxlogf(INFO, "FTL: %d pages of %d bytes\n", num_pages, page_size);
    return true;
}

bool BlockDevice::InitFtl() {
    std::unique_ptr<NandDriver> driver = NandDriver::Create(&parent_, &bad_block_);
    memcpy(guid_, driver->info().partition_guid, ZBI_PARTITION_GUID_LEN);

    if (!volume_) {
        volume_ = std::make_unique<ftl::VolumeImpl>(this);
    }

    const char* error = volume_->Init(std::move(driver));
    if (error) {
        zxlogf(ERROR, "FTL: %s\n", error);
        return false;
    }

    zxlogf(INFO, "FTL: InitFtl ok\n");
    return true;
}

}  // namespace ftl.
