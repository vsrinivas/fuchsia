// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddktl/device.h>
#include <lib/zx/vmo.h>
#include <zircon/device/ramdisk.h>
#include <zircon/types.h>

#include "ramdisk.h"
#include "transaction.h"

namespace ramdisk {
namespace {

class RamdiskController;
using RamdiskControllerDeviceType = ddk::Device<RamdiskController, ddk::Ioctlable>;

class RamdiskController : public RamdiskControllerDeviceType {
public:
    RamdiskController(zx_device_t* parent) : RamdiskControllerDeviceType(parent) {}

    // Device Protocol
    zx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max,
                         size_t* out_actual);
    void DdkRelease() {
        delete this;
    }

private:
    // Other methods
    zx_status_t ConfigureDevice(zx::vmo vmo, uint64_t block_size, uint64_t block_count,
                                uint8_t* type_guid, void* reply, size_t max, size_t* out_actual);
};

zx_status_t RamdiskController::DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen, void* reply,
                                        size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_RAMDISK_CONFIG: {
        if (cmdlen != sizeof(ramdisk_ioctl_config_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        ramdisk_ioctl_config_t* config = (ramdisk_ioctl_config_t*)cmd;
        zx::vmo vmo;
        zx_status_t status = zx::vmo::create( config->blk_size * config->blk_count, 0, &vmo);
        if (status == ZX_OK) {
            status = ConfigureDevice(std::move(vmo), config->blk_size, config->blk_count,
                                     config->type_guid, reply, max, out_actual);
        }
        return status;
    }
    case IOCTL_RAMDISK_CONFIG_VMO: {
        if (cmdlen != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx::vmo vmo = zx::vmo(*reinterpret_cast<const zx_handle_t*>(cmd));

        // Ensure this is the last handle to this VMO; otherwise, the size
        // may change from underneath us.
        zx_info_handle_count_t info;
        zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr,
                                          nullptr);
        if (status != ZX_OK || info.handle_count != 1) {
            return ZX_ERR_INVALID_ARGS;
        }

        uint64_t vmo_size;
        status = vmo.get_size(&vmo_size);
        if (status != ZX_OK) {
            return status;
        }

        return ConfigureDevice(std::move(vmo), PAGE_SIZE, (vmo_size + PAGE_SIZE - 1) / PAGE_SIZE,
                               nullptr, reply, max, out_actual);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

constexpr size_t kMaxRamdiskNameLength = 32;

zx_status_t RamdiskController::ConfigureDevice(zx::vmo vmo, uint64_t block_size,
                                               uint64_t block_count, uint8_t* type_guid,
                                               void* reply, size_t max, size_t* out_actual) {
    if (max < kMaxRamdiskNameLength) {
        return ZX_ERR_INVALID_ARGS;
    }

    std::unique_ptr<Ramdisk> ramdev;
    zx_status_t status = Ramdisk::Create(zxdev(), std::move(vmo), block_size, block_count,
                                         type_guid, &ramdev);
    if (status != ZX_OK) {
        return status;
    }

    char* name = static_cast<char*>(reply);
    size_t namelen = strlcpy(name, ramdev->Name(), max);

    if ((status = ramdev->DdkAdd(ramdev->Name()) != ZX_OK)) {
        ramdev.release()->DdkRelease();
        return status;
    }
    __UNUSED auto ptr = ramdev.release();
    *out_actual = namelen;
    return ZX_OK;
}

zx_status_t RamdiskDriverBind(void* ctx, zx_device_t* parent) {
    auto ramctl = std::make_unique<RamdiskController>(parent);

    zx_status_t status = ramctl->DdkAdd("ramctl");
    if (status != ZX_OK) {
        return status;
    }

    // RamdiskController owned by the DDK after being added successfully.
    __UNUSED auto ptr = ramctl.release();
    return ZX_OK;
}

zx_driver_ops_t ramdisk_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = RamdiskDriverBind;
    return ops;
}();

} // namespace
} // namespace ramdisk

ZIRCON_DRIVER_BEGIN(ramdisk, ramdisk::ramdisk_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ramdisk)
