// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/service/llcpp/service.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "sdk/lib/device-watcher/cpp/device-watcher.h"

namespace {
TEST(FidlProtocolTest, ChildBinds) {
  fbl::unique_fd dev(open("/dev", O_RDONLY));
  ASSERT_TRUE(dev.is_valid());

  // Wait for the child device to bind and appear. The child driver should bind with its string
  // properties. It will then make a call via FIDL and wait for the response before adding the child
  // device.
  fbl::unique_fd fd;
  zx_status_t status = device_watcher::RecursiveWaitForFile(dev, "sys/test/parent/child", &fd);
  ASSERT_OK(status);
}

}  // namespace

int main(int argc, char **argv) {
  namespace fdt = fuchsia_driver_test;
  // Setup DriverTestRealm.
  auto client_end = service::Connect<fdt::Realm>();
  if (!client_end.is_ok()) {
    return 1;
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  fidl::Arena allocator;
  auto response = client->Start(fdt::wire::RealmArgs(allocator));
  if (response.status() != ZX_OK) {
    return 1;
  }
  if (response->result.is_err()) {
    return 1;
  }

  // Run the tests.
  setlinebuf(stdout);
  return RUN_ALL_TESTS(argc, argv);
}
