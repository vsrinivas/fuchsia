// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
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
  void InitUsbHid(fbl::String* devpath,
                  ::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor desc);

  // Unbinds Usb HID driver from host.
  void Unbind(fbl::String devpath);
};

// Initialize a Usb HID device. Asserts on failure.
void USBVirtualBus::InitUsbHid(
    fbl::String* devpath, ::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor desc) {
  namespace usb_peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;
  using ConfigurationDescriptor =
      ::fidl::VectorView<::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor>;
  usb_peripheral::DeviceDescriptor device_desc = {};
  device_desc.bcd_usb = htole16(0x0200);
  device_desc.b_max_packet_size0 = 64;
  device_desc.bcd_device = htole16(0x0100);
  device_desc.b_num_configurations = 1;

  std::vector<usb_peripheral::FunctionDescriptor> function_descs;
  function_descs.push_back(desc);
  std::vector<ConfigurationDescriptor> config_descs;
  config_descs.emplace_back(fidl::unowned_vec(function_descs));

  ASSERT_NO_FATAL_FAILURES(SetupPeripheralDevice(std::move(device_desc), std::move(config_descs)));

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
  auto hid_device_path_response = llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
      zx::unowned_channel(input_channel));
  ASSERT_OK(hid_device_path_response.status());
  zx::channel usbhid_channel;
  std::string hid_device_path = hid_device_path_response->result.response().path.data();
  std::string DEV_CONST = "@/dev/";
  std::string usb_hid_path = hid_device_path.substr(
      DEV_CONST.length(), hid_device_path.find_last_of("/") - DEV_CONST.length());

  fbl::unique_fd fd_usb_hid(openat(GetRootFd(), usb_hid_path.c_str(), O_RDONLY));
  ASSERT_GE(fd_usb_hid.get(), 0);

  ASSERT_OK(fdio_get_service_handle(fd_usb_hid.release(), usbhid_channel.reset_and_get_address()));
  std::string ifc_path = usb_hid_path.substr(0, usb_hid_path.find_last_of('/'));
  fbl::unique_fd fd_usb_hid_parent(openat(GetRootFd(), ifc_path.c_str(), O_DIRECTORY | O_RDONLY));
  ASSERT_GE(fd_usb_hid_parent.get(), 0);
  std::unique_ptr<devmgr_integration_test::DirWatcher> watcher;
  ASSERT_OK(devmgr_integration_test::DirWatcher::Create(std::move(fd_usb_hid_parent), &watcher));
  auto result =
      llcpp::fuchsia::device::Controller::Call::ScheduleUnbind(zx::unowned_channel(usbhid_channel));
  ASSERT_OK(result.status());
  ASSERT_OK(watcher->WaitForRemoval("usb-hid", zx::duration::infinite()));
}

class UsbOneEndpointTest : public zxtest::Test {
 public:
  void SetUp() override {
    ::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor usb_hid_function_desc = {
        .interface_class = USB_CLASS_HID,
        .interface_subclass = 0,
        .interface_protocol = USB_PROTOCOL_TEST_HID_ONE_ENDPOINT,
    };
    ASSERT_NO_FATAL_FAILURES(bus_.InitUsbHid(&devpath_, usb_hid_function_desc));

    fbl::unique_fd fd_input(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
    ASSERT_GT(fd_input.get(), 0);

    zx::channel input_channel;
    ASSERT_OK(fdio_get_service_handle(fd_input.release(), input_channel.reset_and_get_address()));

    sync_client_ = llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(input_channel));
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURES(bus_.ClearPeripheralDeviceFunctions());

    auto result2 = bus_.virtual_bus().Disable();
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result2));
  }

 protected:
  USBVirtualBus bus_;
  fbl::String devpath_;
  std::optional<llcpp::fuchsia::hardware::input::Device::SyncClient> sync_client_;
};

class UsbTwoEndpointTest : public zxtest::Test {
 public:
  void SetUp() override {
    ::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor usb_hid_function_desc = {
        .interface_class = USB_CLASS_HID,
        .interface_subclass = 0,
        .interface_protocol = USB_PROTOCOL_TEST_HID_TWO_ENDPOINT,
    };
    ASSERT_NO_FATAL_FAILURES(bus_.InitUsbHid(&devpath_, usb_hid_function_desc));

    fbl::unique_fd fd_input(openat(bus_.GetRootFd(), devpath_.c_str(), O_RDWR));
    ASSERT_GT(fd_input.get(), 0);

    zx::channel input_channel;
    ASSERT_OK(fdio_get_service_handle(fd_input.release(), input_channel.reset_and_get_address()));

    sync_client_ = llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(input_channel));
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURES(bus_.ClearPeripheralDeviceFunctions());

    auto result2 = bus_.virtual_bus().Disable();
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result2));
  }

 protected:
  USBVirtualBus bus_;
  fbl::String devpath_;
  std::optional<llcpp::fuchsia::hardware::input::Device::SyncClient> sync_client_;
};

TEST_F(UsbOneEndpointTest, SetAndGetReport) {
  uint8_t buf[sizeof(hid_boot_mouse_report_t)] = {0xab, 0xbc, 0xde};

  auto set_result = sync_client_->SetReport(::llcpp::fuchsia::hardware::input::ReportType::INPUT, 0,
                                            fidl::unowned_vec(buf));
  auto get_result =
      sync_client_->GetReport(::llcpp::fuchsia::hardware::input::ReportType::INPUT, 0);

  ASSERT_OK(set_result.status());
  ASSERT_OK(set_result->status);

  ASSERT_OK(get_result.status());
  ASSERT_OK(get_result->status);

  ASSERT_EQ(get_result->report.count(), sizeof(hid_boot_mouse_report_t));
  ASSERT_EQ(0xab, get_result->report[0]);
  ASSERT_EQ(0xbc, get_result->report[1]);
  ASSERT_EQ(0xde, get_result->report[2]);
}

TEST_F(UsbOneEndpointTest, UnBind) { ASSERT_NO_FATAL_FAILURES(bus_.Unbind(devpath_)); }

TEST_F(UsbTwoEndpointTest, SetAndGetReport) {
  uint8_t buf[sizeof(hid_boot_mouse_report_t)] = {0xab, 0xbc, 0xde};

  auto set_result = sync_client_->SetReport(::llcpp::fuchsia::hardware::input::ReportType::INPUT, 0,
                                            fidl::unowned_vec(buf));
  auto get_result =
      sync_client_->GetReport(::llcpp::fuchsia::hardware::input::ReportType::INPUT, 0);

  ASSERT_OK(set_result.status());
  ASSERT_OK(set_result->status);

  ASSERT_OK(get_result.status());
  ASSERT_OK(get_result->status);

  ASSERT_EQ(get_result->report.count(), sizeof(hid_boot_mouse_report_t));
  ASSERT_EQ(0xab, get_result->report[0]);
  ASSERT_EQ(0xbc, get_result->report[1]);
  ASSERT_EQ(0xde, get_result->report[2]);
}

TEST_F(UsbTwoEndpointTest, UnBind) { ASSERT_NO_FATAL_FAILURES(bus_.Unbind(devpath_)); }

}  // namespace
}  // namespace usb_virtual_bus
