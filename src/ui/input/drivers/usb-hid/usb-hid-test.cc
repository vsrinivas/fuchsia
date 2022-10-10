// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.virtual.bus/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/function.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <fbl/string.h>
#include <hid/boot.h>
#include <usb/usb.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() {}

  // Initialize a Usb HID device. Asserts on failure.
  void InitUsbHid(fbl::String* devpath,
                  fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor desc);

  // Unbinds Usb HID driver from host.
  void Unbind(fbl::String devpath);
};

// Initialize a Usb HID device. Asserts on failure.
void USBVirtualBus::InitUsbHid(fbl::String* devpath,
                               fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor desc) {
  namespace usb_peripheral = fuchsia_hardware_usb_peripheral;
  using ConfigurationDescriptor =
      ::fidl::VectorView<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;
  usb_peripheral::wire::DeviceDescriptor device_desc = {};
  device_desc.bcd_usb = htole16(0x0200);
  device_desc.id_vendor = htole16(0x18d1);
  device_desc.id_product = htole16(0xaf10);
  device_desc.b_max_packet_size0 = 64;
  device_desc.bcd_device = htole16(0x0100);
  device_desc.b_num_configurations = 1;

  std::vector<usb_peripheral::wire::FunctionDescriptor> function_descs;
  function_descs.push_back(desc);
  std::vector<ConfigurationDescriptor> config_descs;
  config_descs.emplace_back(
      fidl::VectorView<usb_peripheral::wire::FunctionDescriptor>::FromExternal(function_descs));

  ASSERT_NO_FATAL_FAILURE(SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

  fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/input", O_RDONLY));
  while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, devpath) != ZX_ERR_STOP) {
    continue;
  }
  *devpath = fbl::String::Concat({fbl::String("class/input/"), *devpath});
}

// Unbinds HID driver from host.
void USBVirtualBus::Unbind(fbl::String devpath) {
  fbl::unique_fd fd_input(openat(GetRootFd(), devpath.c_str(), O_RDWR));
  ASSERT_GE(fd_input.get(), 0);
  zx::channel input_channel;
  ASSERT_OK(fdio_get_service_handle(fd_input.release(), input_channel.reset_and_get_address()));
  auto hid_device_path_response =
      fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(input_channel))
          ->GetTopologicalPath();
  ASSERT_OK(hid_device_path_response.status());
  zx::channel usbhid_channel;
  std::string hid_device_path = hid_device_path_response->value()->path.data();
  std::string DEV_CONST = "@/dev/";
  std::string usb_hid_path = hid_device_path.substr(
      DEV_CONST.length(), hid_device_path.find_last_of("/") - DEV_CONST.length());

  fbl::unique_fd fd_usb_hid(openat(GetRootFd(), usb_hid_path.c_str(), O_RDONLY));
  ASSERT_GE(fd_usb_hid.get(), 0);

  ASSERT_OK(fdio_get_service_handle(fd_usb_hid.release(), usbhid_channel.reset_and_get_address()));
  std::string ifc_path = usb_hid_path.substr(0, usb_hid_path.find_last_of('/'));
  fbl::unique_fd fd_usb_hid_parent(openat(GetRootFd(), ifc_path.c_str(), O_DIRECTORY | O_RDONLY));
  ASSERT_GE(fd_usb_hid_parent.get(), 0);
  std::unique_ptr<device_watcher::DirWatcher> watcher;
  ASSERT_OK(device_watcher::DirWatcher::Create(std::move(fd_usb_hid_parent), &watcher));
  auto result = fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(usbhid_channel))
                    ->ScheduleUnbind();
  ASSERT_OK(result.status());
  ASSERT_OK(watcher->WaitForRemoval("usb-hid", zx::duration::infinite()));
}

class UsbOneEndpointTest : public zxtest::Test {
 public:
  void SetUp() override {
    fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor usb_hid_function_desc = {
        .interface_class = USB_CLASS_HID,
        .interface_subclass = 0,
        .interface_protocol = USB_PROTOCOL_TEST_HID_ONE_ENDPOINT,
    };
    ASSERT_NO_FATAL_FAILURE(bus_.InitUsbHid(&devpath_, usb_hid_function_desc));

    fbl::unique_fd fd_input(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
    ASSERT_GT(fd_input.get(), 0);

    zx::channel input_channel;
    ASSERT_OK(fdio_get_service_handle(fd_input.release(), input_channel.reset_and_get_address()));

    sync_client_ = fidl::WireSyncClient<fuchsia_hardware_input::Device>(std::move(input_channel));
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURE(bus_.ClearPeripheralDeviceFunctions());

    auto result2 = bus_.virtual_bus()->Disable();
    ASSERT_NO_FATAL_FAILURE(ValidateResult(result2));
  }

 protected:
  USBVirtualBus bus_;
  fbl::String devpath_;
  fidl::WireSyncClient<fuchsia_hardware_input::Device> sync_client_;
};

class UsbTwoEndpointTest : public zxtest::Test {
 public:
  void SetUp() override {
    fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor usb_hid_function_desc = {
        .interface_class = USB_CLASS_HID,
        .interface_subclass = 0,
        .interface_protocol = USB_PROTOCOL_TEST_HID_TWO_ENDPOINT,
    };
    ASSERT_NO_FATAL_FAILURE(bus_.InitUsbHid(&devpath_, usb_hid_function_desc));

    fbl::unique_fd fd_input(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
    ASSERT_GT(fd_input.get(), 0);

    zx::channel input_channel;
    ASSERT_OK(fdio_get_service_handle(fd_input.release(), input_channel.reset_and_get_address()));

    sync_client_ = fidl::WireSyncClient<fuchsia_hardware_input::Device>(std::move(input_channel));
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURE(bus_.ClearPeripheralDeviceFunctions());

    auto result2 = bus_.virtual_bus()->Disable();
    ASSERT_NO_FATAL_FAILURE(ValidateResult(result2));
  }

 protected:
  USBVirtualBus bus_;
  fbl::String devpath_;
  fidl::WireSyncClient<fuchsia_hardware_input::Device> sync_client_;
};

TEST_F(UsbOneEndpointTest, GetDeviceIdsVidPid) {
  // Check USB device descriptor VID/PID plumbing.
  auto result = sync_client_->GetDeviceIds();
  ASSERT_OK(result.status());
  EXPECT_EQ(0x18d1, result.value().ids.vendor_id);
  EXPECT_EQ(0xaf10, result.value().ids.product_id);
}

TEST_F(UsbOneEndpointTest, SetAndGetReport) {
  uint8_t buf[sizeof(hid_boot_mouse_report_t)] = {0xab, 0xbc, 0xde};

  auto set_result = sync_client_->SetReport(fuchsia_hardware_input::wire::ReportType::kInput, 0,
                                            fidl::VectorView<uint8_t>::FromExternal(buf));
  auto get_result = sync_client_->GetReport(fuchsia_hardware_input::wire::ReportType::kInput, 0);

  ASSERT_OK(set_result.status());
  ASSERT_OK(set_result.value().status);

  ASSERT_OK(get_result.status());
  ASSERT_OK(get_result.value().status);

  ASSERT_EQ(get_result.value().report.count(), sizeof(hid_boot_mouse_report_t));
  ASSERT_EQ(0xab, get_result.value().report[0]);
  ASSERT_EQ(0xbc, get_result.value().report[1]);
  ASSERT_EQ(0xde, get_result.value().report[2]);
}

TEST_F(UsbOneEndpointTest, UnBind) { ASSERT_NO_FATAL_FAILURE(bus_.Unbind(devpath_)); }

TEST_F(UsbTwoEndpointTest, SetAndGetReport) {
  uint8_t buf[sizeof(hid_boot_mouse_report_t)] = {0xab, 0xbc, 0xde};

  auto set_result = sync_client_->SetReport(fuchsia_hardware_input::wire::ReportType::kInput, 0,
                                            fidl::VectorView<uint8_t>::FromExternal(buf));
  auto get_result = sync_client_->GetReport(fuchsia_hardware_input::wire::ReportType::kInput, 0);

  ASSERT_OK(set_result.status());
  ASSERT_OK(set_result.value().status);

  ASSERT_OK(get_result.status());
  ASSERT_OK(get_result.value().status);

  ASSERT_EQ(get_result.value().report.count(), sizeof(hid_boot_mouse_report_t));
  ASSERT_EQ(0xab, get_result.value().report[0]);
  ASSERT_EQ(0xbc, get_result.value().report[1]);
  ASSERT_EQ(0xde, get_result.value().report[2]);
}

TEST_F(UsbTwoEndpointTest, UnBind) { ASSERT_NO_FATAL_FAILURE(bus_.Unbind(devpath_)); }

}  // namespace
}  // namespace usb_virtual_bus
