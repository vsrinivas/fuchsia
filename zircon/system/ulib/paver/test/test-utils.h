// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fzl/vmo-mapper.h>
#include <ramdevice-client/ramdisk.h>
#include <ramdevice-client/ramnand.h>

constexpr uint64_t kBlockSize = 0x1000;
constexpr uint64_t kBlockCount = 0x10;

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kPagesPerBlock = 16;
constexpr uint32_t kSkipBlockSize = kPageSize * kPagesPerBlock;
constexpr uint32_t kNumBlocks = 20;

extern fbl::Vector<fbl::String> test_block_devices;

bool FilterRealBlockDevices(const fbl::unique_fd& fd);

class BlockDevice {
public:
    static void Create(const uint8_t* guid, fbl::unique_ptr<BlockDevice>* device);

    ~BlockDevice() {
        ramdisk_destroy(client_);
    }

private:
    BlockDevice(ramdisk_client_t* client)
        : client_(client) {}

    ramdisk_client_t* client_;
};

class SkipBlockDevice {
public:
    static void Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                       fbl::unique_ptr<SkipBlockDevice>* device);

    fbl::unique_fd devfs_root() { return fbl::unique_fd(dup(ctl_->devfs_root().get())); }

    fzl::VmoMapper& mapper() { return mapper_; }

    ~SkipBlockDevice() = default;

private:
    SkipBlockDevice(fbl::RefPtr<ramdevice_client::RamNandCtl> ctl,
                    ramdevice_client::RamNand ram_nand, fzl::VmoMapper mapper)
        : ctl_(std::move(ctl)), ram_nand_(std::move(ram_nand)), mapper_(std::move(mapper)) {}

    fbl::RefPtr<ramdevice_client::RamNandCtl> ctl_;
    ramdevice_client::RamNand ram_nand_;
    fzl::VmoMapper mapper_;
};

