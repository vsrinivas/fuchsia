// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-bus.h"
#include "helper.h"

#include <ddk/platform-defs.h>
#include <dirent.h>
#include <endian.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>
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

class UsbHidTest : public zxtest::Test {
public:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURES(bus_.InitUsbHid(&devpath_));
        bus_.GetHandles(&peripheral_, &virtual_bus_handle_);
    }

    void TearDown() override {
        ASSERT_EQ(ZX_OK,
                  FidlCall(fuchsia_hardware_usb_peripheral_DeviceClearFunctions, peripheral_->get()));
        ASSERT_EQ(ZX_OK, FidlCall(fuchsia_usb_virtualbus_BusDisable, virtual_bus_handle_->get()));
    }

protected:
    usb_virtual_bus::USBVirtualBus bus_;
    fbl::String devpath_;
    zx::unowned_channel peripheral_;
    zx::unowned_channel virtual_bus_handle_;
};

TEST_F(UsbHidTest, SetAndGetReport) {
    fbl::unique_fd fd_input(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
    ASSERT_GT(fd_input.get(), 0);

    fzl::FdioCaller input_fdio_caller_;
    input_fdio_caller_.reset(std::move(fd_input));

    uint8_t buf[sizeof(hid_boot_mouse_report_t)] = {0xab, 0xbc, 0xde};
    zx_status_t out_stat;
    size_t out_report_count;

    zx_status_t status = fuchsia_hardware_input_DeviceSetReport(
        input_fdio_caller_.borrow_channel(), fuchsia_hardware_input_ReportType_INPUT, 0,
        buf, sizeof(buf), &out_stat);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(out_stat, ZX_OK);

    status = fuchsia_hardware_input_DeviceGetReport(
        input_fdio_caller_.borrow_channel(), fuchsia_hardware_input_ReportType_INPUT, 0, &out_stat,
        buf, sizeof(buf), &out_report_count);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(out_stat, ZX_OK);

    ASSERT_EQ(out_report_count, sizeof(hid_boot_mouse_report_t));
    ASSERT_EQ(0xab, buf[0]);
    ASSERT_EQ(0xbc, buf[1]);
    ASSERT_EQ(0xde, buf[2]);
}

} // namespace
