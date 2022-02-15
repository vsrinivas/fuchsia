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

namespace blobfs {
namespace {

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 8192;

class FakeDriverManagerAdmin final
    : public fidl::WireServer<fuchsia_device_manager::Administrator> {
 public:
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
  }

  void UnregisterSystemStorageForShutdown(
      UnregisterSystemStorageForShutdownRequestView request,
      UnregisterSystemStorageForShutdownCompleter::Sync& completer) override {
    unregister_was_called_ = true;
    completer.Reply(ZX_OK);
  }

  bool UnregisterWasCalled() { return unregister_was_called_; }

 private:
  std::atomic<bool> unregister_was_called_ = false;
};

class BlobfsComponentRunnerTest : public testing::Test {
 public:
  BlobfsComponentRunnerTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

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
    runner_ = std::make_unique<ComponentRunner>(loop_);
    auto status = runner_->ServeRoot(std::move(server_end_),
                                     fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle>(),
                                     std::move(device_admin_client), zx::resource());
    ASSERT_EQ(status.status_value(), ZX_OK);
  }

  fidl::ClientEnd<fuchsia_io::Directory> GetSvcDir() const {
    auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_EQ(svc_endpoints.status_value(), ZX_OK);
    root_->Open(fuchsia_io::wire::kOpenRightReadable | fuchsia_io::wire::kOpenRightWritable,
                fuchsia_io::wire::kModeTypeDirectory, "svc",
                fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
    return std::move(svc_endpoints->client);
  }

  async::Loop loop_;
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

  auto query_client_end = service::ConnectAt<fuchsia_fs::Query>(svc_dir.borrow());
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  fidl::WireSharedClient<fuchsia_fs::Query> query_client(std::move(*query_client_end),
                                                         loop_.dispatcher());

  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  std::atomic<bool> query_complete = false;
  query_client->IsNodeInFilesystem(
      std::move(event), [query_complete = &query_complete](
                            fidl::WireUnownedResult<fuchsia_fs::Query::IsNodeInFilesystem>& info) {
        EXPECT_EQ(info.status(), ZX_OK);
        EXPECT_FALSE(info->is_in_filesystem);
        *query_complete = true;
      });
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);
  ASSERT_TRUE(query_complete);

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

  auto query_client_end = service::ConnectAt<fuchsia_fs::Query>(svc_dir.borrow());
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  fidl::WireSharedClient<fuchsia_fs::Query> query_client(std::move(*query_client_end),
                                                         loop_.dispatcher());

  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  std::atomic<bool> query_complete = false;
  query_client->IsNodeInFilesystem(
      std::move(event), [query_complete = &query_complete](
                            fidl::WireUnownedResult<fuchsia_fs::Query::IsNodeInFilesystem>& info) {
        EXPECT_EQ(info.status(), ZX_OK);
        EXPECT_FALSE(info->is_in_filesystem);
        *query_complete = true;
      });
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);
  ASSERT_TRUE(query_complete);

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

  // Start a call to the query interface. We expect that this request will be queued and won't
  // return until Configure is called on the runner. Initially, GetSvcDir will fire off an open
  // call on the root_ connection, but as the server end isn't serving anything yet, the request is
  // queued there. Once root_ starts serving requests, and the svc dir exists, (which is done by
  // StartServe below) that open call succeeds, but the Query service itself should be waiting to
  // serve any open calls it gets, queuing any requests. Once Configure is called, the Query
  // service should start servicing requests, and the request will succeed.
  auto svc_dir = GetSvcDir();
  auto query_client_end = service::ConnectAt<fuchsia_fs::Query>(svc_dir.borrow());
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  fidl::WireSharedClient<fuchsia_fs::Query> query_client(std::move(*query_client_end),
                                                         loop_.dispatcher());

  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  std::atomic<bool> query_complete = false;
  query_client->IsNodeInFilesystem(
      std::move(event), [query_complete = &query_complete](
                            fidl::WireUnownedResult<fuchsia_fs::Query::IsNodeInFilesystem>& info) {
        EXPECT_EQ(info.status(), ZX_OK);
        EXPECT_FALSE(info->is_in_filesystem);
        *query_complete = true;
      });
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);
  ASSERT_FALSE(query_complete);

  ASSERT_NO_FATAL_FAILURE(StartServe(std::move(admin_endpoints->client)));
  ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK);
  ASSERT_FALSE(query_complete);

  auto client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
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
