// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fuchsia/hardware/input/llcpp/fidl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/syscalls.h>

#include <ddk/platform-defs.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <hid/boot.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() {}

  // Initialize a Usb HID device. Asserts on failure.
  void InitUsbHid(fbl::String* devpath);
};

// Initialize a Usb HID device. Asserts on failure.
void USBVirtualBus::InitUsbHid(fbl::String* devpath) {
  namespace usb_peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;

  usb_peripheral::DeviceDescriptor device_desc = {};
  device_desc.bcdUSB = htole16(0x0200);
  device_desc.bMaxPacketSize0 = 64;
  device_desc.bcdDevice = htole16(0x0100);
  device_desc.bNumConfigurations = 1;

  usb_peripheral::FunctionDescriptor usb_hid_function_desc = {
      .interface_class = USB_CLASS_HID,
      .interface_subclass = 0,
      .interface_protocol = 0,
  };

  std::vector<usb_peripheral::FunctionDescriptor> function_descs;
  function_descs.push_back(usb_hid_function_desc);

  ASSERT_NO_FATAL_FAILURES(SetupPeripheralDevice(device_desc, std::move(function_descs)));

  fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/input", O_RDONLY));
  while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, devpath) != ZX_ERR_STOP) {
    continue;
  }
  *devpath = fbl::String::Concat({fbl::String("class/input/"), *devpath});
}

class UsbHidTest : public zxtest::Test {
 public:
  void SetUp() override { ASSERT_NO_FATAL_FAILURES(bus_.InitUsbHid(&devpath_)); }

  void TearDown() override {
    auto result = bus_.peripheral().ClearFunctions();
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());

    auto result2 = bus_.virtual_bus().Disable();
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result2));
  }

 protected:
  USBVirtualBus bus_;
  fbl::String devpath_;
};

TEST_F(UsbHidTest, SetAndGetReport) {
  fbl::unique_fd fd_input(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
  ASSERT_GT(fd_input.get(), 0);

  zx::channel input_channel;
  ASSERT_OK(fdio_get_service_handle(fd_input.release(), input_channel.reset_and_get_address()));

  llcpp::fuchsia::hardware::input::Device::SyncClient input_client(std::move(input_channel));

  uint8_t buf[sizeof(hid_boot_mouse_report_t)] = {0xab, 0xbc, 0xde};

  auto result = input_client.SetReport(::llcpp::fuchsia::hardware::input::ReportType::INPUT, 0,
                                       fidl::VectorView(sizeof(buf), buf));
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);

  auto result2 = input_client.GetReport(::llcpp::fuchsia::hardware::input::ReportType::INPUT, 0);
  ASSERT_OK(result2.status());
  ASSERT_OK(result2->status);

  ASSERT_EQ(result2->report.count(), sizeof(hid_boot_mouse_report_t));
  ASSERT_EQ(0xab, result2->report[0]);
  ASSERT_EQ(0xbc, result2->report[1]);
  ASSERT_EQ(0xde, result2->report[2]);
}

}  // namespace
}  // namespace usb_virtual_bus
