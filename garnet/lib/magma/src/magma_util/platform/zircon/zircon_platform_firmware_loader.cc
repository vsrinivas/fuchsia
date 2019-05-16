// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_firmware_loader.h"

namespace magma {

class ZirconPlatformFirmwareLoader : public PlatformFirmwareLoader {
public:
    ZirconPlatformFirmwareLoader(zx_device_t* device) : zx_device_(device) {}
    Status LoadFirmware(const char* filename, std::unique_ptr<PlatformBuffer>* firmware_out,
                        uint64_t* size_out) override
    {
        zx::vmo vmo;
        size_t size;
        zx_status_t status =
            load_firmware(zx_device_, filename, vmo.reset_and_get_address(), &size);
        if (status != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failure to load firmware: %d", status);
        }
        *firmware_out = PlatformBuffer::Import(vmo.release());
        *size_out = size;
        return MAGMA_STATUS_OK;
    }

private:
    zx_device_t* zx_device_;
};

std::unique_ptr<PlatformFirmwareLoader> PlatformFirmwareLoader::Create(void* device_handle)
{
    if (!device_handle)
        return DRETP(nullptr, "device_handle is null, cannot create PlatformFirmwareLoader");
    zx_device_t* zx_device = static_cast<zx_device_t*>(device_handle);

    return std::unique_ptr<PlatformFirmwareLoader>(new ZirconPlatformFirmwareLoader(zx_device));
}

} // namespace magma
