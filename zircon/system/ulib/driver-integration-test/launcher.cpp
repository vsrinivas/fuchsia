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

// This board driver knows how to interpret the metadata for which devices to
// spawn.
const zbi_platform_id_t kPlatformId = []() {
    zbi_platform_id_t plat_id = {};
    plat_id.vid = PDEV_VID_TEST;
    plat_id.pid = PDEV_PID_INTEGRATION_TEST;
    strcpy(plat_id.board_name, "driver-integration-test");
    return plat_id;
}();

// This function is responsible for serializing driver data. It must be kept
// updated with the function that deserialized the data. This function
// is TestBoard::FetchAndDeserialize.
zx_status_t GetBootItem(const fbl::Vector<board_test::DeviceEntry>& entries,
                        uint32_t type, uint32_t extra, zx::vmo* out, uint32_t* length) {
    zx::vmo vmo;
    switch (type) {
    case ZBI_TYPE_PLATFORM_ID: {
        zx_status_t status = zx::vmo::create(sizeof(kPlatformId), 0, &vmo);
        if (status != ZX_OK) {
            return status;
        }
        status = vmo.write(&kPlatformId, 0, sizeof(kPlatformId));
        if (status != ZX_OK) {
            return status;
        }
        *length = sizeof(kPlatformId);
        break;
    }
    case ZBI_TYPE_DRV_BOARD_PRIVATE: {
        size_t list_size = sizeof(board_test::DeviceList);
        size_t entry_size = entries.size() * sizeof(board_test::DeviceEntry);

        size_t metadata_size = 0;
        for (board_test::DeviceEntry& entry : entries) {
            metadata_size += entry.metadata_size;
        }

        zx_status_t status = zx::vmo::create(list_size + entry_size + metadata_size, 0, &vmo);
        if (status != ZX_OK) {
            return status;
        }

        // Write DeviceList to vmo.
        board_test::DeviceList list{.count = entries.size()};
        status = vmo.write(&list, 0, sizeof(list));
        if (status != ZX_OK) {
            return status;
        }

        // Write DeviceEntries to vmo.
        status = vmo.write(entries.get(), list_size, entry_size);
        if (status != ZX_OK) {
            return status;
        }

        // Write Metadata to vmo.
        size_t write_offset = list_size + entry_size;
        for (board_test::DeviceEntry& entry : entries) {
            status = vmo.write(entry.metadata, write_offset, entry.metadata_size);
            if (status != ZX_OK) {
                return status;
            }
            write_offset += entry.metadata_size;
        }

        *length = static_cast<uint32_t>(list_size + entry_size + metadata_size);
        break;
    }
    default:
        break;
    }
    *out = std::move(vmo);
    return ZX_OK;
}
zx_status_t GetArguments(const fbl::Vector<const char*>& arguments, zx::vmo* out,
                         uint32_t* length) {
    size_t size = 0;
    for (const char* arg : arguments) {
        size += strlen(arg);
    }

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    size_t offset = 0;
    for (const char* arg : arguments) {
        vmo.write(arg, offset, strlen(arg));
        if (status != ZX_OK) {
            return status;
        }
        offset += strlen(arg);
    }

    *length = static_cast<uint32_t>(size);
    *out = std::move(vmo);
    return status;
}

} // namespace

zx_status_t IsolatedDevmgr::Create(IsolatedDevmgr::Args* args, IsolatedDevmgr* out) {
    IsolatedDevmgr devmgr;

    devmgr_launcher::Args devmgr_args;
    devmgr_args.sys_device_driver = "/boot/driver/platform-bus.so";
    devmgr_args.driver_search_paths.swap(args->driver_search_paths);
    devmgr_args.load_drivers.swap(args->load_drivers);
    devmgr_args.disable_block_watcher = args->disable_block_watcher;
    devmgr_args.disable_netsvc = args->disable_netsvc;
    devmgr_args.get_boot_item = [args](uint32_t type, uint32_t extra, zx::vmo* out,
                                       uint32_t* length) {
        return GetBootItem(args->device_list, type, extra, out, length);
    };
    devmgr_args.get_arguments = [args](zx::vmo* out, uint32_t* length) {
        return GetArguments(args->arguments, out, length);
    };

    zx_status_t status =
        devmgr_integration_test::IsolatedDevmgr::Create(std::move(devmgr_args), &devmgr.devmgr_);
    if (status != ZX_OK) {
        return status;
    }

    *out = std::move(devmgr);
    return ZX_OK;
}

} // namespace driver_integration_test
