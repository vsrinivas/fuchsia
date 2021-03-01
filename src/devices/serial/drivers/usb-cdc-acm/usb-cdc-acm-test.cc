// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <lib/fdio/watcher.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <lib/zx/clock.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/syscalls.h>

#include <ddk/platform-defs.h>
#include <fbl/string.h>
#include <hid/boot.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() = default;

  // Initialize a USB CDC ACM device. Asserts on failure.
  void InitUsbCdcAcm(fbl::String* devpath);
};

// Initialize a USB CDC ACM device. Asserts on failure.
void USBVirtualBus::InitUsbCdcAcm(fbl::String* devpath) {
  namespace usb_peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;
  using ConfigurationDescriptor =
      ::fidl::VectorView<::llcpp::fuchsia::hardware::usb::peripheral::wire::FunctionDescriptor>;
  usb_peripheral::wire::DeviceDescriptor device_desc = {};
  device_desc.bcd_usb = htole16(0x0200);
  device_desc.b_max_packet_size0 = 64;
  device_desc.bcd_device = htole16(0x0100);
  device_desc.b_num_configurations = 1;

  usb_peripheral::wire::FunctionDescriptor usb_cdc_acm_function_desc = {
      .interface_class = USB_CLASS_COMM,
      .interface_subclass = USB_CDC_SUBCLASS_ABSTRACT,
      .interface_protocol = 0,
  };

  std::vector<usb_peripheral::wire::FunctionDescriptor> function_descs;
  function_descs.push_back(usb_cdc_acm_function_desc);
  std::vector<ConfigurationDescriptor> config_descs;
  config_descs.emplace_back(fidl::unowned_vec(function_descs));

  ASSERT_NO_FATAL_FAILURES(SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

  fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/serial", O_RDONLY));
  while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, devpath) != ZX_ERR_STOP) {
  }
  *devpath = fbl::String::Concat({fbl::String("class/serial/"), *devpath});
}

class UsbCdcAcmTest : public zxtest::Test {
 public:
  void SetUp() override { ASSERT_NO_FATAL_FAILURES(bus_.InitUsbCdcAcm(&devpath_)); }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURES(bus_.ClearPeripheralDeviceFunctions());

    auto result2 = bus_.virtual_bus().Disable();
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result2));
  }

  zx_status_t ReadWithTimeout(int fd, void* data, size_t size, size_t* actual) {
    // Time out in 5 seconds.
    const auto timeout = zx::deadline_after(zx::sec(5));
    while (zx::clock::get_monotonic() < timeout) {
      *actual = read(fd, data, size);
      if (*actual != 0) {
        return ZX_OK;
      }
    }
    return ZX_ERR_SHOULD_WAIT;
  }

 protected:
  USBVirtualBus bus_;
  fbl::String devpath_;
};

TEST_F(UsbCdcAcmTest, ReadAndWriteTest) {
  fbl::unique_fd fd(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
  ASSERT_GT(fd.get(), 0);

  uint8_t write_data[] = {1, 2, 3};
  size_t bytes_sent = write(fd.get(), write_data, sizeof(write_data));
  ASSERT_EQ(bytes_sent, sizeof(write_data));

  uint8_t read_data[3] = {};
  zx_status_t status = ReadWithTimeout(fd.get(), read_data, sizeof(read_data), &bytes_sent);
  ASSERT_OK(status);
  ASSERT_EQ(bytes_sent, sizeof(read_data));
  for (size_t i = 0; i < sizeof(write_data); i++) {
    ASSERT_EQ(read_data[i], write_data[i]);
  }

  uint8_t write_data2[] = {5, 4, 3, 2, 1};
  bytes_sent = write(fd.get(), write_data2, sizeof(write_data2));
  ASSERT_EQ(bytes_sent, sizeof(write_data2));

  uint8_t read_data2[5] = {};
  status = ReadWithTimeout(fd.get(), read_data2, sizeof(read_data2), &bytes_sent);
  ASSERT_OK(status);
  ASSERT_EQ(bytes_sent, sizeof(read_data2));
  for (size_t i = 0; i < sizeof(write_data2); i++) {
    ASSERT_EQ(read_data2[i], write_data2[i]);
  }

  // Writing just "0" to the fake USB driver will cause an empty response to be queued.
  uint8_t write_data3[1] = {'0'};
  bytes_sent = write(fd.get(), write_data3, sizeof(write_data3));
  ASSERT_EQ(bytes_sent, sizeof(write_data3));

  uint8_t read_data3[1] = {};
  status = ReadWithTimeout(fd.get(), read_data3, sizeof(read_data3), &bytes_sent);
  ASSERT_EQ(status, ZX_ERR_SHOULD_WAIT);
  ASSERT_EQ(bytes_sent, 0);
}

}  // namespace
}  // namespace usb_virtual_bus
