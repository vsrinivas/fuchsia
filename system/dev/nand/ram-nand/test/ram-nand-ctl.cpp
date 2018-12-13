// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <fs-management/ram-nand.h>
#include <lib/fzl/fdio.h>
#include <unittest/unittest.h>
#include <zircon/nand/c/fidl.h>

#include <utility>

namespace {

zircon_nand_RamNandInfo BuildConfig() {
    zircon_nand_RamNandInfo config = {};
    config.vmo = ZX_HANDLE_INVALID;
    config.nand_info = {4096, 4, 5, 6, 0, zircon_nand_Class_TEST, {}};
    return config;
}

class NandDevice {
  public:
    explicit NandDevice(const zircon_nand_RamNandInfo& config = BuildConfig()) {
        if (fs_mgmt::RamNand::Create(&config, &ram_nand_) == ZX_OK) {
            // caller_ want's to own the device, so we re-open it even though
            // ram_nand_ already has it open.
            fbl::unique_fd device(dup(ram_nand_->fd().get()));
            caller_.reset(std::move(device));
        }
    }

    ~NandDevice() = default;

    bool IsValid() const { return caller_ ? true : false; }

    const char* path() { return ram_nand_->path(); }

  private:

    std::optional<fs_mgmt::RamNand> ram_nand_;
    fzl::FdioCaller caller_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);
};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    fbl::String path;
    {
        NandDevice device;
        ASSERT_TRUE(device.IsValid());
        path = fbl::String(device.path());
    }

    fbl::unique_fd found(open(path.c_str(), O_RDWR));
    ASSERT_FALSE(found);
    END_TEST;
}

bool ExportConfigTest() {
    BEGIN_TEST;
    zircon_nand_RamNandInfo config = BuildConfig();
    config.export_nand_config = true;

    NandDevice device(config);
    ASSERT_TRUE(device.IsValid());
    END_TEST;
}

bool ExportPartitionsTest() {
    BEGIN_TEST;
    zircon_nand_RamNandInfo config = BuildConfig();
    config.export_partition_map = true;

    NandDevice device(config);
    ASSERT_TRUE(device.IsValid());
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(RamNandCtlTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(ExportConfigTest)
RUN_TEST_SMALL(ExportPartitionsTest)
END_TEST_CASE(RamNandCtlTests)
