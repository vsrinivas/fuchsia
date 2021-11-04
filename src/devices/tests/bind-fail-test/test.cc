// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace {

using devmgr_integration_test::IsolatedDevmgr;

TEST(BindFailTest, BindFail) {
  const char kDriver[] = "/boot/driver/bind-fail-test-driver.so";
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  zx::channel sys_chan;
  {
    fbl::unique_fd fd;
    ASSERT_OK(
        devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
    ASSERT_OK(fdio_get_service_handle(fd.release(), sys_chan.reset_and_get_address()));
  }
  fidl::WireSyncClient<fuchsia_device::Controller> sys_dev(std::move(sys_chan));

  auto result = sys_dev->Bind(fidl::StringView{kDriver});
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_err());
  ASSERT_EQ(result->result.err(), ZX_ERR_NOT_SUPPORTED);
}

}  // namespace
