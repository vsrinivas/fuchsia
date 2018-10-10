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

ram_nand_info_t BuildConfig() {
    ram_nand_info_t config = {};
    config.vmo = ZX_HANDLE_INVALID;
    config.nand_info = {4096, 4, 5, 6, 0, NAND_CLASS_DUMMY, {}};
    return config;
}

class NandDevice {
  public:
    NandDevice() {
        const ram_nand_info_t config = BuildConfig();
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

}  // namespace

BEGIN_TEST_CASE(RamNandCtlTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
END_TEST_CASE(RamNandCtlTests)
