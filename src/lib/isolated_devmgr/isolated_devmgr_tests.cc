// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/ethertap/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/device/vfs.h>

#include <ddk/platform-defs.h>

#include "isolated_devmgr.h"

namespace isolated_devmgr {
namespace testing {

constexpr zx::duration kTimeout = zx::sec(30);

class DevmgrTest : public ::gtest::RealLoopFixture {
 protected:
  static constexpr const char* kSysdevDriver = "/boot/driver/test/sysdev.so";
  static constexpr const char* kPlatformDriver = "/boot/driver/platform-bus.so";
  const board_test::DeviceEntry kDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    strcpy(entry.name, "fallback-rtc");
    entry.vid = PDEV_VID_GENERIC;
    entry.pid = PDEV_PID_GENERIC;
    entry.did = PDEV_DID_RTC_FALLBACK;
    return entry;
  }();

  std::unique_ptr<IsolatedDevmgr> CreateDevmgrSysdev() {
    devmgr_launcher::Args args;
    IsolatedDevmgr::ExtraArgs extra_args;
    args.sys_device_driver = kSysdevDriver;
    args.stdio = fbl::unique_fd(open("/dev/null", O_RDWR));
    args.load_drivers.push_back("/boot/driver/ethernet.so");
    args.load_drivers.push_back("/boot/driver/ethertap.so");
    args.disable_block_watcher = true;
    args.disable_netsvc = true;
    return IsolatedDevmgr::Create(std::move(args));
  }

  std::unique_ptr<IsolatedDevmgr> CreateDevmgrPlatTest() {
    devmgr_launcher::Args args;
    auto device_list_ptr = std::unique_ptr<fbl::Vector<board_test::DeviceEntry>>(
        new fbl::Vector<board_test::DeviceEntry>());
    args.sys_device_driver = kPlatformDriver;
    args.stdio = fbl::unique_fd(open("/dev/null", O_RDWR));
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = true;
    args.disable_netsvc = true;
    device_list_ptr->push_back(kDeviceEntry);
    return IsolatedDevmgr::Create(std::move(args), std::move(device_list_ptr));
  }

  fidl::InterfaceHandle<fuchsia::hardware::ethertap::TapDevice> CreateTapDevice(
      const zx::channel& devfs) {
    fidl::SynchronousInterfacePtr<fuchsia::hardware::ethertap::TapControl> tapctl;
    fdio_service_connect_at(devfs.get(), "test/tapctl",
                            tapctl.NewRequest().TakeChannel().release());
    fuchsia::hardware::ethertap::Config config;
    config.mtu = 1500;
    config.options = 0;
    config.features = 0;
    const uint8_t mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    memcpy(config.mac.octets.data(), mac, sizeof(mac));

    fidl::InterfaceHandle<fuchsia::hardware::ethertap::TapDevice> device;
    zx_status_t o_status;
    if (tapctl->OpenDevice("tap_device", std::move(config), device.NewRequest(), &o_status) !=
            ZX_OK ||
        o_status != ZX_OK) {
      // discard channel
      device = nullptr;
    }
    return device;
  }

  void EnableVirtualAudio(const zx::channel& devfs) {
    fuchsia::virtualaudio::ForwarderPtr virtualaudio;
    fdio_service_connect_at(devfs.get(), "test/virtual_audio",
                            virtualaudio.NewRequest().TakeChannel().release());

    // Perform a simple RPC with a reply to sanity check we're talking to the driver.
    fidl::SynchronousInterfacePtr<fuchsia::virtualaudio::Control> control_sync_ptr;
    virtualaudio->SendControl(control_sync_ptr.NewRequest());
    ASSERT_EQ(ZX_OK, control_sync_ptr->Enable());
  }
};

TEST_F(DevmgrTest, CreateTapSysdev) {
  auto devmgr = CreateDevmgrSysdev();
  ASSERT_TRUE(devmgr);
  ASSERT_EQ(devmgr->WaitForFile("test/tapctl"), ZX_OK);

  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  devmgr->Connect(dir.NewRequest().TakeChannel());
  auto tap = CreateTapDevice(dir.channel());
  ASSERT_TRUE(tap);
  // after having created tap, we should be able to see an ethernet device show
  // up:
  ASSERT_EQ(devmgr->WaitForFile("class/ethernet/000"), ZX_OK);
}

TEST_F(DevmgrTest, DeviceEntryEnumerationTest) {
  auto devmgr = CreateDevmgrPlatTest();
  ASSERT_TRUE(devmgr);

  fbl::unique_fd fd;

  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFile(devmgr->devfs_root(),
                                                                 "sys/platform", &fd));
  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFile(devmgr->devfs_root(),
                                                                 "sys/platform/test-board", &fd));
  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFile(
                       devmgr->devfs_root(), "sys/platform/00:00:f/fallback-rtc", &fd));
}

TEST_F(DevmgrTest, DISABLED_ExceptionCallback) {
  auto devmgr = CreateDevmgrSysdev();
  ASSERT_TRUE(devmgr);
  ASSERT_EQ(devmgr->WaitForFile("test/tapctl"), ZX_OK);
  bool exception = false;
  devmgr->SetExceptionCallback([&exception]() { exception = true; });
  // TODO(brunodalbo): Cause devmgr crash here so we can
  //  validate that the exception callback works and
  //  enable this test. There's no good way to cause a crash
  //  today.
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&exception]() { return exception; }, kTimeout));
}

TEST_F(DevmgrTest, ExposedThroughComponent) {
  auto ctx = sys::ComponentContext::Create();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  zx::channel req;
  auto services = sys::ServiceDirectory::CreateWithRequest(&req);

  fuchsia::sys::LaunchInfo info;
  info.directory_request = std::move(req);
  info.url =
      "fuchsia-pkg://fuchsia.com/isolated_devmgr_tests#meta/"
      "isolated_devmgr.cmx";
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr;

  launcher->CreateComponent(std::move(info), ctlr.NewRequest());
  ctlr.set_error_handler([](zx_status_t err) { FAIL() << "Controller shouldn't exit"; });

  zx::channel devfs_req, devfs;
  zx::channel::create(0, &devfs_req, &devfs);
  services->Connect("fuchsia.example.IsolatedDevmgr", std::move(devfs_req));

  auto tap = CreateTapDevice(devfs);
  ASSERT_TRUE(tap);
}

TEST_F(DevmgrTest, ExposeDriverFromComponentNamespace) {
  auto ctx = sys::ComponentContext::Create();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  zx::channel req;
  auto services = sys::ServiceDirectory::CreateWithRequest(&req);

  fuchsia::sys::LaunchInfo info;
  info.directory_request = std::move(req);
  info.url =
      "fuchsia-pkg://fuchsia.com/isolated_devmgr_tests#meta/"
      "isolated_devmgr_virtual_audio.cmx";
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr;

  launcher->CreateComponent(std::move(info), ctlr.NewRequest());
  ctlr.set_error_handler([](zx_status_t err) { FAIL() << "Controller shouldn't exit"; });

  zx::channel devfs_req, devfs;
  zx::channel::create(0, &devfs_req, &devfs);
  services->Connect("fuchsia.example.IsolatedDevmgr", std::move(devfs_req));

  EnableVirtualAudio(devfs);
}

}  // namespace testing
}  // namespace isolated_devmgr
