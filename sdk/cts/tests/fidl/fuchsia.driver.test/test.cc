// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driver/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/device-watcher/cpp/device-watcher.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <zxtest/zxtest.h>

void StartDriverTestRealm() {
  // Connect to DriverTestRealm.
  fuchsia::driver::test::RealmSyncPtr client;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(client.NewRequest());

  // Start the DriverTestRealm with correct arguments.
  fuchsia::driver::test::RealmArgs args;

  auto interface = fidl::InterfaceHandle<fuchsia::io::Directory>();
  zx_status_t status = fdio_open("/pkg",
                                 static_cast<uint32_t>(fuchsia::io::OpenFlags::DIRECTORY |
                                                       fuchsia::io::OpenFlags::RIGHT_READABLE |
                                                       fuchsia::io::OpenFlags::RIGHT_EXECUTABLE),
                                 interface.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return;
  }
  args.set_boot(std::move(interface));
  args.set_root_driver("#driver/test-parent-sys.so");

  fuchsia::driver::test::Realm_Start_Result result;
  auto call_result = client->Start(std::move(args), &result);
  if (call_result != ZX_OK) {
    FX_SLOG(ERROR, "Failed to call to Realm:Start", KV("call_result", call_result));
    return;
  }
  if (result.is_err()) {
    FX_SLOG(ERROR, "Realm:Start failed", KV("error", result.err()));
    return;
  }
}

TEST(DriverTestRealmCts, DriverWasLoaded) {
  syslog::SetTags({"driver_test_realm_test"});
  StartDriverTestRealm();
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test", &out));
}
