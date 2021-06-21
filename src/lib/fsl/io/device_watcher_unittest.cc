// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fsl/io/device_watcher.h"

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fit/defer.h>

#include <fbl/ref_ptr.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace fsl {
namespace {

class DeviceWatcher : public gtest::RealLoopFixture {};

// Test that the callback is never called with ".".
TEST_F(DeviceWatcher, IgnoreDot) {
  async::Loop fs_loop{&kAsyncLoopConfigNeverAttachToThread};

  fs_loop.StartThread("fs-loop");
  auto empty_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  fs::SynchronousVfs vfs(fs_loop.dispatcher());

  fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;

  auto request = dir_handle.NewRequest();
  async::PostTask(fs_loop.dispatcher(), [&, request = std::move(request)]() mutable {
    vfs.Serve(empty_dir, fidl::ServerEnd<fuchsia_io::Node>(request.TakeChannel()),
              fs::VnodeConnectionOptions::ReadWrite());
  });

  fdio_ns_t* ns;
  const char* kDevicePath = "/test-device-path";
  EXPECT_EQ(ZX_OK, fdio_ns_get_installed(&ns));
  EXPECT_EQ(ZX_OK, fdio_ns_bind(ns, kDevicePath, dir_handle.TakeChannel().release()));
  auto defer_unbind = fit::defer([&]() { fdio_ns_unbind(ns, kDevicePath); });
  auto device_watcher = fsl::DeviceWatcher::CreateWithIdleCallback(
      kDevicePath,
      [](int dir_fd, const std::string& filename) {
        // The pseudo-directory is empty, so this callback should never be called.
        EXPECT_TRUE(false) << filename;
      },
      [this]() { QuitLoop(); });
  ASSERT_TRUE(device_watcher);
  // Wait until the idle callback has run.
  RunLoop();

  fs_loop.Shutdown();
}

}  // namespace
}  // namespace fsl
