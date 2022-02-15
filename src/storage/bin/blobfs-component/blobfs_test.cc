// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>
#include <zircon/device/vfs.h>

#include <gtest/gtest.h>

#include "src/storage/testing/ram_disk.h"

namespace blobfs {
namespace {

constexpr uint32_t kBlockCount = 1024 * 256;
constexpr uint32_t kBlockSize = 512;

const fuchsia_component_decl::wire::ChildRef kBlobfsChildRef{.name = "test-blobfs",
                                                             .collection = "blobfs-collection"};

class BlobfsComponentTest : public testing::Test {
 public:
  void SetUp() override {
    auto ramdisk_or = storage::RamDisk::Create(kBlockSize, kBlockCount);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    ramdisk_ = std::move(*ramdisk_or);

    auto realm_client_end = service::Connect<fuchsia_component::Realm>();
    ASSERT_EQ(realm_client_end.status_value(), ZX_OK);
    realm_ = fidl::BindSyncClient(std::move(*realm_client_end));

    fidl::Arena allocator;
    fuchsia_component_decl::wire::CollectionRef collection_ref{.name = "blobfs-collection"};
    fuchsia_component_decl::wire::Child child_decl(allocator);
    child_decl.set_name(allocator, allocator, "test-blobfs")
        .set_url(allocator, allocator, "fuchsia-boot:///#meta/blobfs.cm")
        .set_startup(fuchsia_component_decl::wire::StartupMode::kLazy);
    fuchsia_component::wire::CreateChildArgs child_args;
    auto create_res = realm_->CreateChild(collection_ref, child_decl, child_args);
    ASSERT_EQ(create_res.status(), ZX_OK);
    ASSERT_FALSE(create_res->result.is_err())
        << "create error: " << static_cast<uint32_t>(create_res->result.err());

    auto exposed_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(exposed_endpoints.status_value(), ZX_OK);
    auto open_exposed_res =
        realm_->OpenExposedDir(kBlobfsChildRef, std::move(exposed_endpoints->server));
    ASSERT_EQ(open_exposed_res.status(), ZX_OK);
    ASSERT_FALSE(open_exposed_res->result.is_err())
        << "open exposed dir error: " << static_cast<uint32_t>(open_exposed_res->result.err());
    exposed_dir_ = std::move(exposed_endpoints->client);

    auto startup_client_end =
        service::ConnectAt<fuchsia_fs_startup::Startup>(exposed_dir_.borrow());
    ASSERT_EQ(startup_client_end.status_value(), ZX_OK);
    startup_client_ = fidl::BindSyncClient(std::move(*startup_client_end));
  }

  void TearDown() override {
    auto destroy_res = realm_->DestroyChild(kBlobfsChildRef);
    ASSERT_EQ(destroy_res.status(), ZX_OK);
    ASSERT_FALSE(destroy_res->result.is_err())
        << "destroy error: " << static_cast<uint32_t>(destroy_res->result.err());
  }

  const fidl::WireSyncClient<fuchsia_fs_startup::Startup>& startup_client() const {
    return startup_client_;
  }

  fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir() const {
    return exposed_dir_.borrow();
  }

  fidl::ClientEnd<fuchsia_hardware_block::Block> block_client() const {
    auto block_client_end =
        service::Connect<fuchsia_hardware_block::Block>(ramdisk_.path().c_str());
    EXPECT_EQ(block_client_end.status_value(), ZX_OK);
    auto res = fidl::WireCall(*block_client_end)->GetInfo();
    EXPECT_EQ(res.status(), ZX_OK);
    EXPECT_EQ(res->status, ZX_OK);
    return std::move(*block_client_end);
  }

 private:
  storage::RamDisk ramdisk_;
  fidl::WireSyncClient<fuchsia_component::Realm> realm_;
  fidl::WireSyncClient<fuchsia_fs_startup::Startup> startup_client_;
  fidl::ClientEnd<fuchsia_io::Directory> exposed_dir_;
};

TEST_F(BlobfsComponentTest, FormatCheckStartQuery) {
  fuchsia_fs_startup::wire::FormatOptions format_options;
  auto format_res = startup_client()->Format(block_client(), std::move(format_options));
  ASSERT_EQ(format_res.status(), ZX_OK);
  ASSERT_FALSE(format_res->result.is_err());

  fuchsia_fs_startup::wire::CheckOptions check_options;
  auto check_res = startup_client()->Check(block_client(), std::move(check_options));
  ASSERT_EQ(check_res.status(), ZX_OK);
  ASSERT_FALSE(check_res->result.is_err());

  fuchsia_fs_startup::wire::StartOptions start_options;
  start_options.write_compression_level = -1;
  auto startup_res = startup_client()->Start(block_client(), std::move(start_options));
  ASSERT_EQ(startup_res.status(), ZX_OK);
  ASSERT_FALSE(startup_res->result.is_err());

  auto query_client_end = service::ConnectAt<fuchsia_fs::Query>(exposed_dir());
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  auto query_client = fidl::BindSyncClient(std::move(*query_client_end));

  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  auto query_res = query_client->IsNodeInFilesystem(std::move(event));
  ASSERT_EQ(query_res.status(), ZX_OK);
  ASSERT_FALSE(query_res->is_in_filesystem);

  auto admin_client_end = service::ConnectAt<fuchsia_fs::Admin>(exposed_dir());
  ASSERT_EQ(admin_client_end.status_value(), ZX_OK);
  auto admin_client = fidl::BindSyncClient(std::move(*admin_client_end));

  auto shutdown_res = admin_client->Shutdown();
  ASSERT_EQ(shutdown_res.status(), ZX_OK);
}

TEST_F(BlobfsComponentTest, RequestsBeforeStartupAreQueuedAndServicedAfter) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("blobfs caller test thread");
  auto query_client_end = service::ConnectAt<fuchsia_fs::Query>(exposed_dir());
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  fidl::WireSharedClient query_client(std::move(*query_client_end), loop.dispatcher());

  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  sync_completion_t query_completion;
  query_client->IsNodeInFilesystem(
      std::move(event), [query_completion = &query_completion](
                            fidl::WireUnownedResult<fuchsia_fs::Query::IsNodeInFilesystem>& info) {
        EXPECT_EQ(info.status(), ZX_OK);
        EXPECT_FALSE(info->is_in_filesystem);
        sync_completion_signal(query_completion);
      });

  fuchsia_fs_startup::wire::FormatOptions format_options;
  auto format_res = startup_client()->Format(block_client(), std::move(format_options));
  ASSERT_EQ(format_res.status(), ZX_OK);
  ASSERT_FALSE(format_res->result.is_err());

  fuchsia_fs_startup::wire::CheckOptions check_options;
  auto check_res = startup_client()->Check(block_client(), std::move(check_options));
  ASSERT_EQ(check_res.status(), ZX_OK);
  ASSERT_FALSE(check_res->result.is_err());

  fuchsia_fs_startup::wire::StartOptions start_options;
  start_options.write_compression_level = -1;
  auto startup_res = startup_client()->Start(block_client(), std::move(start_options));
  ASSERT_EQ(startup_res.status(), ZX_OK);
  ASSERT_FALSE(startup_res->result.is_err());

  // Query should get a response now.
  EXPECT_EQ(sync_completion_wait(&query_completion, ZX_TIME_INFINITE), ZX_OK);

  auto admin_client_end = service::ConnectAt<fuchsia_fs::Admin>(exposed_dir());
  ASSERT_EQ(admin_client_end.status_value(), ZX_OK);
  auto admin_client = fidl::BindSyncClient(std::move(*admin_client_end));

  auto shutdown_res = admin_client->Shutdown();
  ASSERT_EQ(shutdown_res.status(), ZX_OK);
}

}  // namespace
}  // namespace blobfs
