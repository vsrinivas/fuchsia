// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/driver/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/device-watcher/cpp/device-watcher.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>
#include <zircon/device/vfs.h>

#include <zxtest/zxtest.h>

void StartDriverTestRealm() {
  // Connect to DriverTestRealm.
  fuchsia::driver::test::RealmSyncPtr client;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(client.NewRequest());

  // Start the DriverTestRealm with correct arguments.
  fuchsia::driver::test::RealmArgs args;

  auto interface = fidl::InterfaceHandle<fuchsia::io::Directory>();
  zx_status_t status =
      fdio_open("/pkg", ZX_FS_FLAG_DIRECTORY | ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
                interface.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return;
  }
  args.set_boot(std::move(interface));
  args.set_root_driver("#driver/test-parent-sys.so");

  fuchsia::driver::test::Realm_Start_Result result;
  auto call_result = client->Start(std::move(args), &result);
  if (call_result != ZX_OK) {
    FX_LOGF(ERROR, "driver_test_realm_test", "Failed to call to Realm:Start: %d", call_result);
    return;
  }
  if (result.is_err()) {
    FX_LOGF(ERROR, "driver_test_realm_test", "Realm:Start failed: %d", result.err());
    return;
  }
}

TEST(DriverTestRealmCts, DriverWasLoaded) {
  StartDriverTestRealm();
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test", &out));
}
