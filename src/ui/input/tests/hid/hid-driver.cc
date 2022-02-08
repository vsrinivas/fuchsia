// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.hidctl/cpp/wire.h>
#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

#include <hid/boot.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

namespace {

class HidDriverTest : public zxtest::Test {
 protected:
  void SetUp() override {
    // Create and build the realm.
    auto realm_builder = component_testing::RealmBuilder::Create();
    driver_test_realm::Setup(realm_builder);
    realm_ =
        std::make_unique<component_testing::RealmRoot>(realm_builder.Build(loop_.dispatcher()));

    // Start DriverTestRealm.
    ASSERT_EQ(ZX_OK, realm_->Connect(driver_test_realm.NewRequest()));
    fuchsia::driver::test::Realm_Start_Result realm_result;

    auto args = fuchsia::driver::test::RealmArgs();
#ifdef DFV2
    args.set_use_driver_framework_v2(true);
    args.set_root_driver("fuchsia-boot:///#meta/test-parent-sys.cm");
#endif

    ASSERT_EQ(ZX_OK, driver_test_realm->Start(std::move(args), &realm_result));
    ASSERT_FALSE(realm_result.is_err());

    // Connect to dev.
    fidl::InterfaceHandle<fuchsia::io::Directory> dev;
    zx_status_t status = realm_->Connect("dev", dev.NewRequest().TakeChannel());
    ASSERT_EQ(status, ZX_OK);

    status = fdio_fd_create(dev.TakeChannel().release(), dev_fd_.reset_and_get_address());
    ASSERT_EQ(status, ZX_OK);

    // Wait for HidCtl to be created
    fbl::unique_fd hidctl_fd;
    ASSERT_OK(device_watcher::RecursiveWaitForFile(dev_fd_, "sys/test/hidctl", &hidctl_fd));

    // Get a FIDL channel to HidCtl
    zx_handle_t handle;
    ASSERT_OK(fdio_get_service_handle(hidctl_fd.release(), &handle));
    hidctl_client_ =
        fidl::WireSyncClient(fidl::ClientEnd<fuchsia_hardware_hidctl::Device>(zx::channel(handle)));
  }

  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fbl::unique_fd dev_fd_;
  fidl::WireSyncClient<fuchsia_hardware_hidctl::Device> hidctl_client_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
};

const uint8_t kBootMouseReportDesc[50] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Button)
    0x19, 0x01,  //     Usage Minimum (0x01)
    0x29, 0x03,  //     Usage Maximum (0x03)
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x95, 0x03,  //     Report Count (3)
    0x75, 0x01,  //     Report Size (1)
    0x81, 0x02,  //     Input (Data,Var,Abs,No Wrap,Linear,No Null Position)
    0x95, 0x01,  //     Report Count (1)
    0x75, 0x05,  //     Report Size (5)
    0x81, 0x03,  //     Input (Const,Var,Abs,No Wrap,Linear,No Null Position)
    0x05, 0x01,  //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x02,  //     Report Count (2)
    0x81, 0x06,  //     Input (Data,Var,Rel,No Wrap,Linear,No Null Position)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};

// We cannot test the HID driver directly in DFv2 because it still uses Open() which
// is not supported.
#ifndef DFV2
TEST_F(HidDriverTest, BootMouseTest) {
  // Create a fake mouse device
  fuchsia_hardware_hidctl::wire::HidCtlConfig config = {};
  config.dev_num = 5;
  config.boot_device = false;
  config.dev_class = 0;
  auto result = hidctl_client_->MakeHidDevice(
      config, fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(kBootMouseReportDesc),
                                                      std::size(kBootMouseReportDesc)));
  ASSERT_OK(result.status());
  zx::socket hidctl_socket = std::move(result->report_socket);

  // Open the corresponding /dev/class/input/ device
  fbl::unique_fd fd_device;
  zx_status_t status = device_watcher::RecursiveWaitForFile(dev_fd_, "class/input/000", &fd_device);
  ASSERT_OK(status);

  // Send a single mouse report
  hid_boot_mouse_report_t mouse_report = {};
  mouse_report.rel_x = 50;
  mouse_report.rel_y = 100;
  status = hidctl_socket.write(0, &mouse_report, sizeof(mouse_report), NULL);
  ASSERT_OK(status);

  // Open a FIDL channel to the HID device
  zx::channel chan;
  ASSERT_OK(fdio_get_service_handle(fd_device.get(), chan.reset_and_get_address()));
  auto client =
      fidl::WireSyncClient(fidl::ClientEnd<fuchsia_hardware_input::Device>(std::move(chan)));

  // Get the report event.
  zx::event report_event;
  {
    auto result = client->GetReportsEvent();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    report_event = std::move(result->event);
  }

  // Check that the report comes through
  {
    report_event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);

    hid_boot_mouse_report_t test_report = {};

    auto response = client->ReadReport();
    ASSERT_OK(response.status());
    ASSERT_OK(response->status);
    ASSERT_EQ(response->data.count(), sizeof(test_report));

    memcpy(&test_report, response->data.data(), sizeof(test_report));
    ASSERT_EQ(mouse_report.rel_x, test_report.rel_x);
    ASSERT_EQ(mouse_report.rel_y, test_report.rel_y);
  }

  // Check that report descriptors match completely
  {
    auto response = client->GetReportDesc();
    ASSERT_OK(response.status());
    ASSERT_EQ(response->desc.count(), sizeof(kBootMouseReportDesc));
    for (size_t i = 0; i < sizeof(kBootMouseReportDesc); i++) {
      if (kBootMouseReportDesc[i] != response->desc[i]) {
        printf("Index %ld of the report descriptor doesn't match\n", i);
      }
      EXPECT_EQ(kBootMouseReportDesc[i], response->desc[i]);
    }
  }
}
#endif

TEST_F(HidDriverTest, BootMouseTestInputReport) {
  // Create a fake mouse device
  fuchsia_hardware_hidctl::wire::HidCtlConfig config;
  config.dev_num = 5;
  config.boot_device = false;
  config.dev_class = 0;
  auto result = hidctl_client_->MakeHidDevice(
      config, fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(kBootMouseReportDesc),
                                                      std::size(kBootMouseReportDesc)));
  ASSERT_OK(result.status());
  zx::socket hidctl_socket = std::move(result->report_socket);

  // Open the corresponding /dev/class/input/ device
  fbl::unique_fd fd_device;
  zx_status_t status =
      device_watcher::RecursiveWaitForFile(dev_fd_, "class/input-report/000", &fd_device);
  ASSERT_OK(status);
  zx::channel chan;
  ASSERT_OK(fdio_get_service_handle(fd_device.get(), chan.reset_and_get_address()));
  auto client =
      fidl::WireSyncClient(fidl::ClientEnd<fuchsia_input_report::InputDevice>(std::move(chan)));

  auto reader_endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_OK(reader_endpoints.status_value());
  auto reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
      std::move(reader_endpoints->client));
  client->GetInputReportsReader(std::move(reader_endpoints->server));

  // Check the Descriptor.
  {
    auto response = client->GetDescriptor();
    ASSERT_OK(response.status());
    ASSERT_TRUE(response->descriptor.has_mouse());
    ASSERT_TRUE(response->descriptor.mouse().has_input());
    ASSERT_TRUE(response->descriptor.mouse().input().has_movement_x());
    ASSERT_TRUE(response->descriptor.mouse().input().has_movement_y());
  }

  // Send a single mouse report
  hid_boot_mouse_report_t mouse_report = {};
  mouse_report.rel_x = 50;
  mouse_report.rel_y = 100;
  status = hidctl_socket.write(0, &mouse_report, sizeof(mouse_report), nullptr);
  ASSERT_OK(status);

  // Get the mouse InputReport.
  auto response = reader->ReadInputReports();
  ASSERT_OK(response.status());
  ASSERT_OK(response.status());
  ASSERT_EQ(1, response->result.response().reports.count());
  ASSERT_EQ(50, response->result.response().reports[0].mouse().movement_x());
  ASSERT_EQ(100, response->result.response().reports[0].mouse().movement_y());
}

}  // namespace
