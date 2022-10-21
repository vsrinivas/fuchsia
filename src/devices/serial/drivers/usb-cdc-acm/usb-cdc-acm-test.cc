// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.serial/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.virtual.bus/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/watcher.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <fbl/string.h>
#include <hid/boot.h>
#include <usb/cdc.h>
#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {

using usb_virtual::BusLauncher;

class UsbCdcAcmTest : public zxtest::Test {
 public:
  void SetUp() override {
    auto bus = BusLauncher::Create();
    ASSERT_OK(bus.status_value());
    bus_ = std::move(bus.value());
    ASSERT_NO_FATAL_FAILURE(InitUsbCdcAcm(&devpath_));
  }

  void TearDown() override {
    ASSERT_OK(bus_->ClearPeripheralDeviceFunctions());

    ASSERT_OK(bus_->Disable());
  }

 protected:
  // Initialize a USB CDC ACM device. Asserts on failure.
  void InitUsbCdcAcm(fbl::String* devpath) {
    namespace usb_peripheral = fuchsia_hardware_usb_peripheral;
    using ConfigurationDescriptor =
        ::fidl::VectorView<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;
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
    config_descs.emplace_back(
        fidl::VectorView<usb_peripheral::wire::FunctionDescriptor>::FromExternal(function_descs));

    ASSERT_OK(bus_->SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

    fbl::unique_fd fd(openat(bus_->GetRootFd(), "class/serial", O_RDONLY));
    while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, devpath) !=
           ZX_ERR_STOP) {
    }
    *devpath = fbl::String::Concat({fbl::String("class/serial/"), *devpath});
  }

  std::optional<BusLauncher> bus_;
  fbl::String devpath_;
};

TEST_F(UsbCdcAcmTest, ReadAndWriteTest) {
  zx::result result = component::ConnectAt<fuchsia_hardware_serial::Device>(
      fdio_cpp::UnownedFdioCaller(bus_->GetRootFd()).directory(), devpath_.c_str());
  ASSERT_OK(result.status_value());
  fidl::ClientEnd<fuchsia_hardware_serial::Device>& client_end = result.value();

  auto assert_read_with_timeout = [&client_end](cpp20::span<uint8_t> write_data) {
    for (zx::time deadline = zx::deadline_after(zx::sec(5));
         zx::clock::get_monotonic() < deadline;) {
      const fidl::WireResult result = fidl::WireCall(client_end)->Read();
      ASSERT_OK(result.status());
      const fit::result response = result.value();
      ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
      cpp20::span data = response.value()->data.get();
      if (data.empty()) {
        continue;
      }
      ASSERT_EQ(data.size_bytes(), write_data.size_bytes());
      ASSERT_BYTES_EQ(data.data(), write_data.data(), write_data.size_bytes());
      return;
    }
    FAIL("timed out");
  };

  {
    uint8_t write_data[] = {1, 2, 3};
    const fidl::WireResult result =
        fidl::WireCall(client_end)->Write(fidl::VectorView<uint8_t>::FromExternal(write_data));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_NO_FATAL_FAILURE(assert_read_with_timeout(cpp20::span(write_data)));
  }

  {
    uint8_t write_data[] = {5, 4, 3, 2, 1};
    const fidl::WireResult result =
        fidl::WireCall(client_end)->Write(fidl::VectorView<uint8_t>::FromExternal(write_data));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_NO_FATAL_FAILURE(assert_read_with_timeout(cpp20::span(write_data)));
  }

  {
    // Writing just "0" to the fake USB driver will cause an empty response to be queued.
    uint8_t write_data[] = {'0'};
    const fidl::WireResult result =
        fidl::WireCall(client_end)->Write(fidl::VectorView<uint8_t>::FromExternal(write_data));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));

    for (zx::time deadline = zx::deadline_after(zx::sec(5));
         zx::clock::get_monotonic() < deadline;) {
      const fidl::WireResult result = fidl::WireCall(client_end)->Read();
      ASSERT_OK(result.status());
      const fit::result response = result.value();
      ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
      cpp20::span data = response.value()->data.get();
      ASSERT_TRUE(data.empty(), "%zu", data.size());
    }
  }
}

}  // namespace
}  // namespace usb_virtual_bus
