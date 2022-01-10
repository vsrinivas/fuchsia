// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

class BlobfsComponentTest : public testing::Test {
 public:
  void SetUp() override {
    auto ramdisk_or = storage::RamDisk::Create(kBlockSize, kBlockCount);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    ramdisk_ = std::move(*ramdisk_or);

    auto startup_client_end = service::Connect<fuchsia_fs_startup::Startup>();
    ASSERT_EQ(startup_client_end.status_value(), ZX_OK);
    startup_client_ = fidl::BindSyncClient(std::move(*startup_client_end));
  }
  void TearDown() override {}

  const fidl::WireSyncClient<fuchsia_fs_startup::Startup>& startup_client() const {
    return startup_client_;
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
  fidl::WireSyncClient<fuchsia_fs_startup::Startup> startup_client_;
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

  auto query_client_end = service::Connect<fuchsia_fs::Query>();
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  auto query_client = fidl::BindSyncClient(std::move(*query_client_end));

  auto query_res = query_client->GetInfo();
  ASSERT_EQ(query_res.status(), ZX_OK);
  ASSERT_FALSE(query_res->result.is_err()) << query_res->result.err();
  ASSERT_EQ(query_res->result.response().info.fs_type, VFS_TYPE_BLOBFS);

  auto admin_client_end = service::Connect<fuchsia_fs::Admin>();
  ASSERT_EQ(admin_client_end.status_value(), ZX_OK);
  auto admin_client = fidl::BindSyncClient(std::move(*admin_client_end));

  auto shutdown_res = admin_client->Shutdown();
  ASSERT_EQ(shutdown_res.status(), ZX_OK);
}

TEST_F(BlobfsComponentTest, RequestsBeforeStartupAreQueuedAndServicedAfter) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("blobfs caller test thread");
  auto query_client_end = service::Connect<fuchsia_fs::Query>();
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  fidl::WireSharedClient query_client(std::move(*query_client_end), loop.dispatcher());

  sync_completion_t query_completion;
  query_client->GetInfo([query_completion = &query_completion](
                            fidl::WireUnownedResult<fuchsia_fs::Query::GetInfo>& info) {
    EXPECT_EQ(info.status(), ZX_OK);
    EXPECT_TRUE(info->result.is_response()) << zx_status_get_string(info->result.err());
    EXPECT_EQ(info->result.response().info.fs_type, VFS_TYPE_BLOBFS);
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

  auto admin_client_end = service::Connect<fuchsia_fs::Admin>();
  ASSERT_EQ(admin_client_end.status_value(), ZX_OK);
  auto admin_client = fidl::BindSyncClient(std::move(*admin_client_end));

  auto shutdown_res = admin_client->Shutdown();
  ASSERT_EQ(shutdown_res.status(), ZX_OK);
}

}  // namespace
}  // namespace blobfs
