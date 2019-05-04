// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/test-utils.h"

#include <limits.h>

#include <optional>

#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/vector.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/fdio.h>
#include <lib/paver/device-partitioner.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zxtest/zxtest.h>

namespace {

void InsertTestDevices(fbl::StringPiece path) {
    zx::channel device, device_remote;
    ASSERT_EQ(zx::channel::create(0, &device, &device_remote), ZX_OK);
    ASSERT_EQ(fdio_service_connect(path.data(), device_remote.release()), ZX_OK);

    char topo_path[PATH_MAX] = {};
    zx_status_t call_status;
    size_t path_len;
    ASSERT_EQ(fuchsia_device_ControllerGetTopologicalPath(device.get(), &call_status, topo_path,
                                                          sizeof(topo_path) - 1, &path_len),
              ZX_OK);
    ASSERT_EQ(call_status, ZX_OK);
    topo_path[path_len] = 0;

    fbl::String topo_path_str(topo_path);
    test_block_devices.push_back(std::move(topo_path_str));
}

void CreateBadBlockMap(void* buffer) {
    // Set all entries in first BBT to be good blocks.
    constexpr uint8_t kBlockGood = 0;
    memset(buffer, kBlockGood, kPageSize);

    struct OobMetadata {
        uint32_t magic;
        int16_t program_erase_cycles;
        uint16_t generation;
    };

    const size_t oob_offset = kPageSize * kPagesPerBlock * kNumBlocks;
    auto* oob = reinterpret_cast<OobMetadata*>(reinterpret_cast<uintptr_t>(buffer) + oob_offset);
    oob->magic = 0x7462626E; // "nbbt"
    oob->program_erase_cycles = 0;
    oob->generation = 1;
}

} // namespace

fbl::Vector<fbl::String> test_block_devices;

bool FilterRealBlockDevices(const fbl::unique_fd& fd) {
    char topo_path[PATH_MAX] = {'\0'};

    fzl::UnownedFdioCaller caller(fd.get());
    zx_status_t call_status;
    size_t path_len;
    zx_status_t status = fuchsia_device_ControllerGetTopologicalPath(
        caller.borrow_channel(), &call_status, topo_path, sizeof(topo_path) - 1,
        &path_len);
    if (status != ZX_OK || call_status != ZX_OK) {
        return true;
    }
    topo_path[path_len] = 0;

    for (const auto& device : test_block_devices) {
        if (strstr(topo_path, device.data()) == topo_path) {
            return false;
        }
    }
    return true;
}

void BlockDevice::Create(const uint8_t* guid, fbl::unique_ptr<BlockDevice>* device) {
    ramdisk_client_t* client;
    ASSERT_EQ(ramdisk_create_with_guid(kBlockSize, kBlockCount, guid, ZBI_PARTITION_GUID_LEN,
                                       &client),
              ZX_OK);
    InsertTestDevices(ramdisk_get_path(client));
    device->reset(new BlockDevice(client));
}

void SkipBlockDevice::Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                             fbl::unique_ptr<SkipBlockDevice>* device) {
    fzl::VmoMapper mapper;
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, mapper.CreateAndMap((kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks,
                                         ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
    memset(mapper.start(), 0xff, mapper.size());
    CreateBadBlockMap(mapper.start());
    vmo.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, mapper.size(), nullptr, 0);
    zx::vmo dup;
    ASSERT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);

    fuchsia_hardware_nand_RamNandInfo info = nand_info;
    info.vmo = dup.release();
    fbl::RefPtr<ramdevice_client::RamNandCtl> ctl;
    ASSERT_EQ(ramdevice_client::RamNandCtl::Create(&ctl), ZX_OK);
    std::optional<ramdevice_client::RamNand> ram_nand;
    ASSERT_EQ(ramdevice_client::RamNand::Create(ctl, &info, &ram_nand), ZX_OK);
    device->reset(new SkipBlockDevice(std::move(ctl), *std::move(ram_nand),
                                      std::move(mapper)));
}
