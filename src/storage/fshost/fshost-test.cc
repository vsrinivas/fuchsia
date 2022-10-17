// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <gtest/gtest.h>

#include "config.h"
#include "fs-manager.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/storage/fshost/block-watcher.h"

namespace fshost {
namespace {

namespace fio = fuchsia_io;

// Test that the manager Shutdown fails if ReadyForShutdown is not called.
TEST(FsManagerTestCase, ShutdownBeforeReadyFails) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  FsManager manager(nullptr);
  auto config = EmptyConfig();
  BlockWatcher watcher(manager, &config);
  ASSERT_EQ(manager.Initialize({}, {}, config, watcher), ZX_OK);

  sync_completion_t callback_called;
  manager.Shutdown([callback_called = &callback_called](zx_status_t status) {
    sync_completion_signal(callback_called);
  });
  EXPECT_FALSE(sync_completion_signaled(&callback_called));
  manager.ReadyForShutdown();
  sync_completion_wait(&callback_called, ZX_TIME_INFINITE);
}

// Test that the manager performs the shutdown procedure correctly with respect to externally
// observable behaviors.
TEST(FsManagerTestCase, ShutdownSignalsCompletion) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  FsManager manager(nullptr);
  auto config = EmptyConfig();
  BlockWatcher watcher(manager, &config);
  ASSERT_EQ(manager.Initialize({}, {}, config, watcher), ZX_OK);

  manager.ReadyForShutdown();
  // The manager should not have exited yet: No one has asked for the shutdown.
  EXPECT_FALSE(manager.IsShutdown());

  // Once we trigger shutdown, we expect a shutdown signal.
  sync_completion_t callback_called;
  manager.Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    sync_completion_signal(callback_called);
  });
  manager.WaitForShutdown();
  EXPECT_EQ(sync_completion_wait(&callback_called, ZX_TIME_INFINITE), ZX_OK);

  // It's an error if shutdown gets called twice, but we expect the callback to still get called
  // with the appropriate error message since the shutdown function has no return value.
  sync_completion_reset(&callback_called);
  manager.Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_INTERNAL);
    sync_completion_signal(callback_called);
  });
  EXPECT_EQ(sync_completion_wait(&callback_called, ZX_TIME_INFINITE), ZX_OK);
}

// Test that the manager shuts down the filesystems given a call on the lifecycle channel
TEST(FsManagerTestCase, LifecycleStop) {
  zx::result create_lifecycle = fidl::CreateEndpoints<fuchsia_process_lifecycle::Lifecycle>();
  ASSERT_TRUE(create_lifecycle.is_ok()) << create_lifecycle.status_string();
  auto [lifecycle_client, lifecycle_server] = std::move(create_lifecycle).value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  FsManager manager(nullptr);
  auto config = DefaultConfig();
  BlockWatcher watcher(manager, &config);
  ASSERT_EQ(manager.Initialize({}, std::move(lifecycle_server), config, watcher), ZX_OK);

  manager.ReadyForShutdown();
  // The manager should not have exited yet: No one has asked for an unmount.
  EXPECT_FALSE(manager.IsShutdown());

  // Call stop on the lifecycle channel
  auto result = fidl::WireCall(lifecycle_client)->Stop();
  ASSERT_EQ(result.status(), ZX_OK);

  // the lifecycle channel should be closed now
  zx_signals_t pending;
  EXPECT_EQ(
      lifecycle_client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &pending),
      ZX_OK);
  EXPECT_TRUE(pending & ZX_CHANNEL_PEER_CLOSED);

  // Now we expect a shutdown signal.
  manager.WaitForShutdown();
}

class MockDirectoryOpener : public fidl::testing::WireTestBase<fuchsia_io::Directory> {
 public:
  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    ADD_FAILURE() << "Unexpected call to MockDirectoryOpener: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Open(OpenRequestView request, OpenCompleter::Sync& completer) override {
    saved_open_flags_ = request->flags;
    saved_open_count_ += 1;
    saved_path_ = request->path.get();
  }

  fio::wire::OpenFlags saved_open_flags() const { return saved_open_flags_; }
  uint32_t saved_open_count() const { return saved_open_count_; }
  const std::string& saved_path() const { return saved_path_; }

 private:
  // Test fields used for validation.
  fio::wire::OpenFlags saved_open_flags_ = {};
  uint32_t saved_open_count_ = 0;
  std::string saved_path_;
};

TEST(FsManagerTestCase, InstallFsAfterShutdownWillFail) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  FsManager manager(nullptr);
  auto config = EmptyConfig();
  config.durable() = true;
  BlockWatcher watcher(manager, &config);
  ASSERT_EQ(manager.Initialize({}, {}, config, watcher), ZX_OK);

  manager.ReadyForShutdown();
  manager.Shutdown([](zx_status_t status) { EXPECT_EQ(status, ZX_OK); });
  manager.WaitForShutdown();

  EXPECT_FALSE(manager.TakeMountPointServerEnd(FsManager::MountPoint::kDurable).has_value());
}

TEST(FsManagerTestCase, ReportFailureOnUncleanUnmount) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  FsManager manager(nullptr);
  auto config = EmptyConfig();
  config.durable() = true;
  BlockWatcher watcher(manager, &config);
  ASSERT_EQ(manager.Initialize({}, {}, config, watcher), ZX_OK);

  std::optional endpoints_or =
      manager.TakeMountPointServerEnd(FsManager::MountPoint::kDurable, true);
  ASSERT_TRUE(endpoints_or.has_value());
  auto [export_root, server_end] = std::move(endpoints_or.value());
  server_end.Close(ZX_ERR_INTERNAL);

  manager.ReadyForShutdown();

  zx_status_t shutdown_status = ZX_OK;
  manager.Shutdown([&shutdown_status](zx_status_t status) { shutdown_status = status; });
  manager.WaitForShutdown();

  // We closed the server end we got back, which should cause shutdown to receive PEER_CLOSED when
  // it tries to shut down the filesystem.
  ASSERT_EQ(shutdown_status, ZX_ERR_PEER_CLOSED);
}

}  // namespace
}  // namespace fshost
