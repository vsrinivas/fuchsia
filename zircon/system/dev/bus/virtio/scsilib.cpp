// Copyriht 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scsilib.h"

#include <ddk/protocol/block.h>
#include <fbl/alloc_checker.h>

namespace scsi {

zx_status_t Disk::Create(zx_device_t* parent, uint8_t target, uint16_t lun) {
    fbl::AllocChecker ac;
    auto* const disk = new (&ac) scsi::Disk(parent, /*target=*/target, /*lun=*/lun);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = disk->Bind();
    if (status != ZX_OK) {
        delete disk;
    }
    return status;
}

zx_status_t Disk::Bind() {
    return DdkAdd(tag_);
}

Disk::Disk(zx_device_t* parent, uint8_t target, uint16_t lun)
    : DeviceType(parent), target_(target), lun_(lun) {
    snprintf(tag_, sizeof(tag_), "scsi-disk-%d-%d", target_, lun_);
}

} // namespace scsi
