// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/resource.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/blobfs/component_runner.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/mount.h"

namespace blobfs {
namespace {

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 8192;

class FakeDriverManagerAdmin final
    : public fidl::WireServer<fuchsia_device_manager::Administrator> {
 public:
  void UnregisterSystemStorageForShutdown(
      UnregisterSystemStorageForShutdownRequestView request,
      UnregisterSystemStorageForShutdownCompleter::Sync& completer) override {
    unregister_was_called_ = true;
    completer.Reply(ZX_OK);
  }

  void SuspendWithoutExit(SuspendWithoutExitRequestView request,
                          SuspendWithoutExitCompleter::Sync& completer) override {}

  bool UnregisterWasCalled() { return unregister_was_called_; }

 private:
  std::atomic<bool> unregister_was_called_ = false;
};

class BlobfsComponentRunnerTest : public testing::Test {
 public:
  BlobfsComponentRunnerTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        config_(ComponentOptions{.pager_threads = 1}) {}

  void SetUp() override {
    device_ = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_EQ(FormatFilesystem(device_.get(), FilesystemOptions{}), ZX_OK);

    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    root_ = fidl::BindSyncClient(std::move(endpoints->client));
    server_end_ = std::move(endpoints->server);
  }
  void TearDown() override {}

  void StartServe(fidl::ClientEnd<fuchsia_device_manager::Administrator> device_admin_client) {
    runner_ = std::make_unique<ComponentRunner>(loop_, config_);
    auto status = runner_->ServeRoot(std::move(server_end_),
                                     fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle>(),
                                     std::move(device_admin_client), zx::resource());
    ASSERT_EQ(status.status_value(), ZX_OK);
  }

  fidl::ClientEnd<fuchsia_io::Directory> GetSvcDir() const {
    auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_EQ(svc_endpoints.status_value(), ZX_OK);
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)root_->Open(
        fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable,
        fuchsia_io::wire::kModeTypeDirectory, "svc",
        fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
    return std::move(svc_endpoints->client);
  }

  async::Loop loop_;
  ComponentOptions config_;
  std::unique_ptr<block_client::FakeBlockDevice> device_;
  std::unique_ptr<ComponentRunner> runner_;
  fidl::WireSyncClient<fuchsia_io::Directory> root_;
  fidl::ServerEnd<fuchsia_io::Directory> server_end_;
};

TEST_F(BlobfsComponentRunnerTest, ServeAndConfigureStartsBlobfs) {
  FakeDriverManagerAdmin driver_admin;
  auto admin_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Administrator>();
  ASSERT_TRUE(admin_endpoints.is_ok());
  fidl::BindServer(loop_.dispatcher(), std::move(admin_endpoints->server), &driver_admin);

  ASSERT_NO_FATAL_FAILURE(StartServe(std::move(admin_endpoints->client)));

  auto svc_dir = GetSvcDir();
  auto client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
  ASSERT_EQ(client_end.status_value(), ZX_OK);

  MountOptions options;
  auto status = runner_->Configure(std::move(device_), options);
  ASSERT_EQ(status.status_value(), ZX_OK);

  std::atomic<bool> callback_called = false;
  runner_->Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    *callback_called = true;
  });
  // Shutdown quits the loop.
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_ERR_CANCELED);
  ASSERT_TRUE(callback_called);

  EXPECT_TRUE(driver_admin.UnregisterWasCalled());
}

TEST_F(BlobfsComponentRunnerTest, ServeAndConfigureStartsBlobfsWithoutDriverManager) {
  ASSERT_NO_FATAL_FAILURE(StartServe(fidl::ClientEnd<fuchsia_device_manager::Administrator>()));

  auto svc_dir = GetSvcDir();
  auto client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
  ASSERT_EQ(client_end.status_value(), ZX_OK);

  MountOptions options;
  auto status = runner_->Configure(std::move(device_), options);
  ASSERT_EQ(status.status_value(), ZX_OK);

  std::atomic<bool> callback_called = false;
  runner_->Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    *callback_called = true;
  });
  // Shutdown quits the loop.
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_ERR_CANCELED);
  ASSERT_TRUE(callback_called);
}

TEST_F(BlobfsComponentRunnerTest, RequestsBeforeStartupAreQueuedAndServicedAfter) {
  FakeDriverManagerAdmin driver_admin;
  auto admin_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Administrator>();
  ASSERT_TRUE(admin_endpoints.is_ok());
  fidl::BindServer(loop_.dispatcher(), std::move(admin_endpoints->server), &driver_admin);

  ASSERT_NO_FATAL_FAILURE(StartServe(std::move(admin_endpoints->client)));
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);

  auto svc_dir = GetSvcDir();
  auto client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
  ASSERT_EQ(client_end.status_value(), ZX_OK);

  MountOptions options;
  auto status = runner_->Configure(std::move(device_), options);
  ASSERT_EQ(status.status_value(), ZX_OK);
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);

  std::atomic<bool> callback_called = false;
  runner_->Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    *callback_called = true;
  });
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_ERR_CANCELED);
  ASSERT_TRUE(callback_called);

  EXPECT_TRUE(driver_admin.UnregisterWasCalled());
}

}  // namespace
}  // namespace blobfs
