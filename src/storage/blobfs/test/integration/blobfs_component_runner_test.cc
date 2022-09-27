// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/service_client.h>
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
      UnregisterSystemStorageForShutdownCompleter::Sync& completer) override {
    unregister_was_called_ = true;
    completer.Reply(ZX_OK);
  }

  void SuspendWithoutExit(SuspendWithoutExitCompleter::Sync& completer) override {}

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
    root_ = std::move(endpoints->client);
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
    ZX_ASSERT(svc_endpoints.status_value() == ZX_OK);
    auto status = fidl::WireCall(root_)->Open(
        fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable,
        fuchsia_io::wire::kModeTypeDirectory, "svc",
        fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
    ZX_ASSERT(status.status() == ZX_OK);
    return std::move(svc_endpoints->client);
  }

  fidl::ClientEnd<fuchsia_io::Directory> GetRootDir() const {
    auto root_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ZX_ASSERT(root_endpoints.status_value() == ZX_OK);
    auto status = fidl::WireCall(root_)->Open(
        fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable,
        fuchsia_io::wire::kModeTypeDirectory, "root",
        fidl::ServerEnd<fuchsia_io::Node>(root_endpoints->server.TakeChannel()));
    ZX_ASSERT(status.status() == ZX_OK);
    return std::move(root_endpoints->client);
  }

  async::Loop loop_;
  ComponentOptions config_;
  std::unique_ptr<block_client::FakeBlockDevice> device_;
  std::unique_ptr<ComponentRunner> runner_;
  fidl::ClientEnd<fuchsia_io::Directory> root_;
  fidl::ServerEnd<fuchsia_io::Directory> server_end_;
};

TEST_F(BlobfsComponentRunnerTest, ServeAndConfigureStartsBlobfs) {
  FakeDriverManagerAdmin driver_admin;
  auto admin_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Administrator>();
  ASSERT_TRUE(admin_endpoints.is_ok());
  fidl::BindServer(loop_.dispatcher(), std::move(admin_endpoints->server), &driver_admin);

  ASSERT_NO_FATAL_FAILURE(StartServe(std::move(admin_endpoints->client)));

  auto svc_dir = GetSvcDir();
  auto client_end = component::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
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
  auto client_end = component::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
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

  // Start a call to the filesystem. We expect that this request will be queued and won't return
  // until Configure is called on the runner. Initially, GetSvcDir will fire off an open call on
  // the root_ connection, but as the server end isn't serving anything yet, the request is queued
  // there. Once root_ starts serving requests, and the svc dir exists, (which is done by
  // StartServe below) that open call succeeds, but the root itself should be waiting to serve any
  // open calls it gets, queuing any requests. Once Configure is called, the root should start
  // servicing requests, and the request will succeed.
  auto root_dir = GetRootDir();
  fidl::WireSharedClient<fuchsia_io::Directory> root_client(std::move(root_dir),
                                                            loop_.dispatcher());

  std::atomic<bool> query_complete = false;
  root_client->QueryFilesystem().ThenExactlyOnce(
      [query_complete =
           &query_complete](fidl::WireUnownedResult<fuchsia_io::Directory::QueryFilesystem>& info) {
        EXPECT_EQ(info.status(), ZX_OK);
        EXPECT_EQ(info->s, ZX_OK);
        *query_complete = true;
      });
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);
  ASSERT_FALSE(query_complete);

  ASSERT_NO_FATAL_FAILURE(StartServe(std::move(admin_endpoints->client)));
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);
  ASSERT_FALSE(query_complete);

  auto svc_dir = GetSvcDir();
  auto client_end = component::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
  ASSERT_EQ(client_end.status_value(), ZX_OK);

  MountOptions options;
  auto status = runner_->Configure(std::move(device_), options);
  ASSERT_EQ(status.status_value(), ZX_OK);
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);
  ASSERT_TRUE(query_complete);

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
