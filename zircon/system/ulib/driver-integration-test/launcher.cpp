// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-integration-test/fixture.h>

#include <memory.h>
#include <string.h>

#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/zx/vmo.h>
#include <libzbi/zbi-cpp.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

namespace driver_integration_test {

namespace {

constexpr char kBoardName[] = "driver-integration-test";

// This board driver knows how to intepret the metadata for which devices to
// spawn.
const zbi_platform_id_t kPlatformId = []() {
    zbi_platform_id_t plat_id = {};
    plat_id.vid = PDEV_VID_TEST;
    plat_id.pid = PDEV_PID_INTEGRATION_TEST;
    strcpy(plat_id.board_name, kBoardName);
    return plat_id;
}();

zx_status_t GetBootdata(const fbl::Vector<board_test::DeviceEntry>& device_list,
                        zx::vmo* bootdata_out) {
    const size_t metadata_size = sizeof(board_test::DeviceList) +
                                 device_list.size() * sizeof(board_test::DeviceEntry);
    ZX_ASSERT(metadata_size < 1024);

    // Serialize to expected metadata format.
    fbl::unique_ptr<uint8_t[]> metadata(new uint8_t[metadata_size]);
    auto* devices = reinterpret_cast<board_test::DeviceList*>(metadata.get());
    devices->count = device_list.size();
    memcpy(devices->list, device_list.get(), devices->count * sizeof(board_test::DeviceEntry));

    uint8_t zbi_buf[1024];
    zbi::Zbi zbi(zbi_buf, sizeof(zbi_buf));
    if (zbi.Reset() != ZBI_RESULT_OK) {
        return ZX_ERR_INTERNAL;
    }
    if (zbi.AppendSection(sizeof(kPlatformId), ZBI_TYPE_PLATFORM_ID, 0, ZBI_FLAG_VERSION,
                          &kPlatformId) != ZBI_RESULT_OK) {
        return ZX_ERR_INTERNAL;
    }
    if (zbi.AppendSection(static_cast<uint32_t>(metadata_size), ZBI_TYPE_DRV_BOARD_PRIVATE,
                          0, ZBI_FLAG_VERSION, metadata.get()) != ZBI_RESULT_OK) {
        return ZX_ERR_INTERNAL;
    }
    zx::vmo zbi_vmo;
    zx_status_t status = zx::vmo::create(zbi.Length(), 0, &zbi_vmo);
    if (status != ZX_OK) {
        return status;
    }
    status = zbi_vmo.write(zbi.Base(), 0, zbi.Length());
    if (status != ZX_OK) {
        return status;
    }

    *bootdata_out = std::move(zbi_vmo);
    return ZX_OK;
}

} // namespace

zx_status_t IsolatedDevmgr::Create(IsolatedDevmgr::Args args,
                                   IsolatedDevmgr* out) {
    IsolatedDevmgr devmgr;

    devmgr_launcher::Args devmgr_args;
    devmgr_args.sys_device_driver = "/boot/driver/platform-bus.so";
    devmgr_args.driver_search_paths.swap(args.driver_search_paths);
    devmgr_args.load_drivers.swap(args.load_drivers);
    devmgr_args.disable_block_watcher = args.disable_block_watcher;

    zx_status_t status = GetBootdata(args.device_list, &devmgr_args.bootdata);
    if (status != ZX_OK) {
        return status;
    }

    status = devmgr_integration_test::IsolatedDevmgr::Create(std::move(devmgr_args),
                                                             &devmgr.devmgr_);
    if (status != ZX_OK) {
        return status;
    }

    *out = std::move(devmgr);
    return ZX_OK;
}

} // namespace driver_integration_test
