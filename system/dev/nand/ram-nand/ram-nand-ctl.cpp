// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <zircon/device/ram-nand.h>
#include <zircon/types.h>

#include "ram-nand.h"
#include "ram-nand-ctl.h"

namespace {

class RamNandCtl;
using RamNandCtlDeviceType = ddk::Device<RamNandCtl, ddk::Ioctlable>;

class RamNandCtl : public RamNandCtlDeviceType {
  public:
    explicit RamNandCtl(zx_device_t* parent) : RamNandCtlDeviceType(parent) {}

    zx_status_t Bind() { return DdkAdd("nand-ctl"); }
    void DdkRelease() { delete this; }

    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);
  private:
    zx_status_t CreateDevice(const NandParams& params, void* out_buf, size_t* out_actual);
    DISALLOW_COPY_ASSIGN_AND_MOVE(RamNandCtl);
};

zx_status_t RamNandCtl::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    if (out_len < sizeof(ram_nand_name_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (op) {
    case IOCTL_RAM_NAND_CREATE: {
        if (in_len != sizeof(nand_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        const NandParams* params = static_cast<const NandParams*>(in_buf);
        return CreateDevice(*params, out_buf, out_actual);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t RamNandCtl::CreateDevice(const NandParams& params, void* out_buf,
                                     size_t* out_actual) {
    fbl::AllocChecker checker;
    fbl::unique_ptr<NandDevice> device(new (&checker) NandDevice(params, zxdev()));
    if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->Bind();
    if (status != ZX_OK) {
        return status;
    }

    *out_actual = strlen(device->name());
    strcpy(static_cast<char*>(out_buf), device->name());

    // devmgr is now in charge of the device.
    __UNUSED NandDevice* dummy = device.release();
    return ZX_OK;
}

}  // namespace

zx_status_t ram_nand_driver_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker checker;
    fbl::unique_ptr<RamNandCtl> device(new (&checker) RamNandCtl(parent));
    if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the device.
        __UNUSED RamNandCtl* dummy = device.release();
    }
    return status;
}
