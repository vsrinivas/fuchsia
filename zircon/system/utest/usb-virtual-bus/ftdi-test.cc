// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"
#include "usb-virtual-bus.h"
#include <ctime>

#include <ddk/platform-defs.h>
#include <dirent.h>
#include <endian.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/c/fidl.h>
#include <hid/boot.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/bind.h>
#include <lib/fzl/fdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

namespace {

class FtdiTest : public zxtest::Test {
public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURES(bus_.InitFtdi(&devpath_));
        bus_.GetHandles(&peripheral_, &virtual_bus_handle_);
    }

    void TearDown() override {
        ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_peripheral_DeviceClearFunctions,
                                  peripheral_->get()));
        ASSERT_EQ(ZX_OK, FidlCall(fuchsia_hardware_usb_virtual_bus_BusDisable,
                                  virtual_bus_handle_->get()));
    }

    zx_status_t ReadWithTimeout(int fd, void* data, size_t size, size_t* actual) {
        // time out in 50 milliseconds.
        constexpr int timeout_length = 50000;
        auto timeout = std::time(0) + timeout_length;
        while (std::time(0) < timeout) {
            *actual = read(fd, data, size);
            if (*actual != 0) {
                return ZX_OK;
            }
        }
        return ZX_ERR_SHOULD_WAIT;
    }

protected:
    usb_virtual_bus::USBVirtualBus bus_;
    fbl::String devpath_;
    zx::unowned_channel peripheral_;
    zx::unowned_channel virtual_bus_handle_;
};

TEST_F(FtdiTest, ReadAndWriteTest) {
    fbl::unique_fd fd(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
    ASSERT_GT(fd.get(), 0);

    uint8_t write_data[] = {1, 2, 3};
    size_t bytes_sent = write(fd.get(), write_data, sizeof(write_data));
    ASSERT_EQ(bytes_sent, sizeof(write_data));

    uint8_t read_data[3] = {};
    zx_status_t status = ReadWithTimeout(fd.get(), read_data, sizeof(read_data), &bytes_sent);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(bytes_sent, sizeof(read_data));
    for (size_t i = 0; i < sizeof(write_data); i++) {
        ASSERT_EQ(read_data[i], write_data[i]);
    }

    uint8_t write_data2[] = {5, 4, 3, 2, 1};
    bytes_sent = write(fd.get(), write_data2, sizeof(write_data2));
    ASSERT_EQ(bytes_sent, sizeof(write_data2));

    uint8_t read_data2[5] = {};
    status = ReadWithTimeout(fd.get(), read_data2, sizeof(read_data2), &bytes_sent);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(bytes_sent, sizeof(read_data2));
    for (size_t i = 0; i < sizeof(write_data2); i++) {
        ASSERT_EQ(read_data2[i], write_data2[i]);
    }
}

} // namespace
