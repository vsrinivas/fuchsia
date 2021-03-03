// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/device/vfs.h>

#include <zxtest/zxtest.h>

namespace {

using devmgr_integration_test::IsolatedDevmgr;

TEST(MetadataTest, RunTests) {
  const char kDriver[] = "/boot/driver/ddk-metadata-test.so";
  auto args = IsolatedDevmgr::DefaultArgs();

  args.driver_search_paths.push_back("/boot/driver");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  zx::channel sys_chan;
  {
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd));
    ASSERT_OK(fdio_get_service_handle(fd.release(), sys_chan.reset_and_get_address()));
  }
  ::fuchsia_device::Controller::SyncClient sys_dev(std::move(sys_chan));

  auto result = sys_dev.Bind(fidl::StringView{kDriver});
  ASSERT_OK(result.status());
  // The driver will run its tests in its bind routine, and return ZX_OK on success.
  ASSERT_FALSE(result->result.is_err());
}

}  // namespace
