// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <fs-management/ram-nand.h>
#include <unittest/unittest.h>
#include <zircon/device/ram-nand.h>

namespace {

class NandDevice {
  public:
    NandDevice() {
        nand_info_t config = {4096, 4, 5, 6, 0};
        char path[PATH_MAX];
        if (!create_ram_nand(&config, path)) {
            device_.reset(open(path, O_RDWR));
        }
    }

    ~NandDevice() { Unlink(); }

    bool IsValid() const { return device_ ? true : false; }

    int get() const { return device_.get(); }

    bool Unlink() {
        if (device_ && ioctl_ram_nand_unlink(device_.get()) != ZX_OK) {
            return false;
        }
        return true;
    }

  private:
    fbl::unique_fd device_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);
};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());
    ASSERT_TRUE(device.Unlink());
    END_TEST;
}

bool SetBadBlocksTest() {
    BEGIN_TEST;
    NandDevice device;
    uint32_t bad_blocks[] = {1, 3, 5};
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
              ioctl_ram_nand_set_bad_blocks(device.get(), bad_blocks, sizeof(bad_blocks)));
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(RamNandCtlTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(SetBadBlocksTest)
END_TEST_CASE(RamNandCtlTests)
