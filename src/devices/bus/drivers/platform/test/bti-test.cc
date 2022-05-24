// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.btitest/cpp/wire.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/zx/time.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

namespace {

using device_watcher::RecursiveWaitForFile;

using namespace component_testing;

constexpr char kParentPath[] = "sys/platform/11:01:1a";
constexpr char kDevicePath[] = "sys/platform/11:01:1a/test-bti";

TEST(PbusBtiTest, BtiIsSameAfterCrash) {
  auto realm_builder = component_testing::RealmBuilder::Create();
  driver_test_realm::Setup(realm_builder);
  realm_builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.boot.RootResource"}},
                               .source = {ParentRef()},
                               .targets = {ChildRef{"driver_test_realm"}}});

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto realm = realm_builder.Build(loop.dispatcher());

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  ASSERT_EQ(ZX_OK, realm.Connect(driver_test_realm.NewRequest()));
  fuchsia::driver::test::Realm_Start_Result realm_result;
  auto args = fuchsia::driver::test::RealmArgs();
  args.set_root_driver("fuchsia-boot:///#driver/platform-bus.so");
  ASSERT_EQ(ZX_OK, driver_test_realm->Start(std::move(args), &realm_result));
  ASSERT_FALSE(realm_result.is_err());

  // Connect to dev.
  fidl::InterfaceHandle<fuchsia::io::Directory> dev;
  zx_status_t status = realm.Connect("dev", dev.NewRequest().TakeChannel());
  ASSERT_EQ(status, ZX_OK);

  fbl::unique_fd dev_fd;
  status = fdio_fd_create(dev.TakeChannel().release(), dev_fd.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  fbl::unique_fd fd;
  EXPECT_OK(RecursiveWaitForFile(dev_fd, kDevicePath, &fd));
  zx::status bti_client_end =
      fdio_cpp::FdioCaller(std::move(fd)).take_as<fuchsia_hardware_btitest::BtiDevice>();
  ASSERT_OK(bti_client_end.status_value());

  fidl::WireSyncClient client(std::move(*bti_client_end));
  uint64_t koid1;
  {
    auto result = client->GetKoid();
    ASSERT_OK(result.status());
    koid1 = result.value_NEW().koid;
  }

  fd.reset(openat(dev_fd.get(), kParentPath, O_DIRECTORY | O_RDONLY));
  std::unique_ptr<device_watcher::DirWatcher> watcher;
  ASSERT_OK(device_watcher::DirWatcher::Create(std::move(fd), &watcher));

  {
    auto result = client->Crash();
    ASSERT_OK(result.status());
  }

  // We implicitly rely on driver host being rebound in the event of a crash.
  ASSERT_OK(watcher->WaitForRemoval("test-bti", zx::duration::infinite()));
  EXPECT_OK(RecursiveWaitForFile(dev_fd, kDevicePath, &fd));
  bti_client_end =
      fdio_cpp::FdioCaller(std::move(fd)).take_as<fuchsia_hardware_btitest::BtiDevice>();
  ASSERT_OK(bti_client_end.status_value());
  client = fidl::BindSyncClient(std::move(*bti_client_end));

  uint64_t koid2;
  {
    auto result = client->GetKoid();
    ASSERT_OK(result.status());
    koid2 = result.value_NEW().koid;
  }
  ASSERT_EQ(koid1, koid2);
}

}  // namespace
