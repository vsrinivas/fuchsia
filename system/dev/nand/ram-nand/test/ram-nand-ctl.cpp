// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <fs-management/ram-nand.h>
#include <lib/fzl/fdio.h>
#include <unittest/unittest.h>
#include <zircon/device/ram-nand.h>
#include <zircon/nand/c/fidl.h>

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
        if (create_ram_nand(&config, path_) == ZX_OK) {
            fbl::unique_fd device(open(path_, O_RDWR));
            caller_.reset(fbl::move(device));
        }
    }

    ~NandDevice() { Unlink(); }

    bool IsValid() const { return caller_ ? true : false; }

    const char* path() const { return path_; }

    bool Unlink() {
        if (!caller_) {
            return false;
        }
        zx_status_t status;
        return zircon_nand_RamNandUnlink(caller_.borrow_channel(), &status) == ZX_OK &&
               status == ZX_OK;
    }

  private:

    fzl::FdioCaller caller_;
    char path_[PATH_MAX];
    DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);
};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());
    ASSERT_TRUE(device.Unlink());

    fbl::unique_fd found(open(device.path(), O_RDWR));
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
