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
#include "src/lib/files/glob.h"

namespace isolated_devmgr {
namespace testing {

class DevmgrTest : public ::gtest::RealLoopFixture {
 protected:
  static constexpr const char* kSysdevDriver = "/boot/driver/test/sysdev.so";
  static constexpr const char* kPlatformDriver = "/boot/driver/platform-bus.so";
  const board_test::DeviceEntry kRtcDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    strcpy(entry.name, "fallback-rtc");
    entry.vid = PDEV_VID_GENERIC;
    entry.pid = PDEV_PID_GENERIC;
    entry.did = PDEV_DID_RTC_FALLBACK;
    return entry;
  }();

  const board_test::DeviceEntry kCrashDeviceEntry = []() {
    board_test::DeviceEntry entry = {};
    strcpy(entry.name, "crash-device");
    entry.vid = PDEV_VID_GENERIC;
    entry.pid = PDEV_PID_GENERIC;
    entry.did = PDEV_DID_CRASH_TEST;
    return entry;
  }();

  std::unique_ptr<IsolatedDevmgr> CreateDevmgrSysdev() {
    devmgr_launcher::Args args;
    IsolatedDevmgr::ExtraArgs extra_args;
    args.sys_device_driver = kSysdevDriver;
    args.path_prefix = "/pkg/";
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
    args.path_prefix = "/pkg/";
    args.stdio = fbl::unique_fd(open("/dev/null", O_RDWR));
    args.driver_search_paths.push_back("/boot/driver");
    args.driver_search_paths.push_back("/boot/driver/test");
    args.disable_block_watcher = true;
    args.disable_netsvc = true;
    device_list_ptr->push_back(kRtcDeviceEntry);
    device_list_ptr->push_back(kCrashDeviceEntry);
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
    zx_status_t status, o_status;
    if ((status = tapctl->OpenDevice("tap_device", std::move(config), device.NewRequest(),
                                     &o_status)) != ZX_OK ||
        o_status != ZX_OK) {
      fprintf(stderr, "status: %d, o_status: %d\n", status, o_status);
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

TEST_F(DevmgrTest, ExceptionCallback) {
  auto devmgr = CreateDevmgrPlatTest();
  ASSERT_TRUE(devmgr);

  bool exception = false;
  devmgr->SetExceptionCallback([&exception]() { exception = true; });

  ASSERT_EQ(devmgr->WaitForFile("sys/platform/00:00:24"), ZX_OK);

  zx_handle_t dir;
  ASSERT_EQ(fdio_get_service_handle(devmgr->root(), &dir), ZX_OK);

  RunLoopUntil([&exception, &dir]() {
    // keep trying to open crash-device until we see an exception
    zx::channel a, b;
    EXPECT_EQ(zx::channel::create(0, &a, &b), ZX_OK);
    fdio_service_connect_at(dir, "sys/platform/00:00:24/crash-device", b.release());

    return exception;
  });
}

TEST_F(DevmgrTest, ExposedThroughComponent) {
  auto ctx = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  zx::channel req;
  auto services = sys::ServiceDirectory::CreateWithRequest(&req);

  fuchsia::sys::LaunchInfo info;
  info.directory_request = std::move(req);
  info.url =
      "fuchsia-pkg://fuchsia.com/isolated-devmgr-tests-package#meta/"
      "ethertap-devmgr.cmx";
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
  auto ctx = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  zx::channel req;
  auto services = sys::ServiceDirectory::CreateWithRequest(&req);

  fuchsia::sys::LaunchInfo info;
  info.directory_request = std::move(req);
  info.url =
      "fuchsia-pkg://fuchsia.com/isolated-devmgr-tests-package#meta/"
      "virtual-audio-devmgr.cmx";
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr;

  launcher->CreateComponent(std::move(info), ctlr.NewRequest());
  ctlr.set_error_handler([](zx_status_t err) { FAIL() << "Controller shouldn't exit"; });

  zx::channel devfs_req, devfs;
  zx::channel::create(0, &devfs_req, &devfs);
  services->Connect("fuchsia.example.IsolatedDevmgr", std::move(devfs_req));

  EnableVirtualAudio(devfs);
}

TEST_F(DevmgrTest, ExposeDevfsToHub) {
  auto ctx = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  fuchsia::sys::LaunchInfo info;
  info.url =
      "fuchsia-pkg://fuchsia.com/isolated-devmgr-tests-package#meta/virtual-audio-devmgr.cmx";
  auto echo_svc = sys::ServiceDirectory::CreateWithRequest(&info.directory_request);
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr;
  launcher->CreateComponent(std::move(info), ctlr.NewRequest());

  ctlr.set_error_handler([](zx_status_t err) { FAIL() << "Controller shouldn't exit"; });
  bool ready = false;
  ctlr.events().OnDirectoryReady = [&ready] { ready = true; };
  RunLoopUntil([&ready] { return ready; });
  ASSERT_TRUE(ready);

  // Verify that devfs is indeed visible in outgoing directory
  constexpr char kGlob[] = "/hub/c/virtual-audio-devmgr.cmx/*/out/dev";
  files::Glob glob(kGlob);
  EXPECT_EQ(glob.size(), 1u) << kGlob << " expected to match exactly once.";
}

TEST_F(DevmgrTest, DiagnosticsFiles) {
  auto devmgr = CreateDevmgrSysdev();
  ASSERT_TRUE(devmgr);

  fbl::unique_fd fd;
  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFileReadOnly(devmgr->devfs_root(),
                                                                         "diagnostics", &fd));
  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFileReadOnly(devmgr->devfs_root(),
                                                                         "diagnostics/class", &fd));
  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFileReadOnly(
                       devmgr->devfs_root(), "diagnostics/driver_manager", &fd));
  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFileReadOnly(
                       devmgr->devfs_root(), "diagnostics/driver_manager/dm.inspect", &fd));
  ASSERT_EQ(ZX_OK, devmgr_integration_test::RecursiveWaitForFileReadOnly(
                       devmgr->devfs_root(), "diagnostics/driver_manager/driver_host", &fd));

  // TODO(fxbug.dev/50569): Add test for root,sys,misc,test driver_host files once koids are
  // available via dm.inspect
}

}  // namespace testing
}  // namespace isolated_devmgr
