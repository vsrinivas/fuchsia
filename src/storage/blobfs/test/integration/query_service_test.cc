// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fs/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/zx/vmo.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include <fs/test_support/test_support.h>
#include <fvm/format.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"

namespace blobfs {
namespace {

namespace fio = ::llcpp::fuchsia::io;
namespace fuchsia_fs = ::llcpp::fuchsia::fs;

class QueryServiceTest : public BlobfsTestWithFvm {
 protected:
  fuchsia_fs::Query::SyncClient ConnectToQueryService() {
    zx::channel query_client_end, query_server_end;
    EXPECT_EQ(zx::channel::create(0, &query_client_end, &query_server_end), ZX_OK);
    std::string query_service_path = std::string("svc/") + fuchsia_fs::Query::Name;
    EXPECT_EQ(fdio_service_connect_at(fs().GetOutgoingDirectory()->get(),
                                      query_service_path.c_str(), query_server_end.release()),
              ZX_OK);
    return fuchsia_fs::Query::SyncClient(std::move(query_client_end));
  }

  void QueryInfo(size_t expected_nodes, size_t expected_bytes) {
    fuchsia_fs::Query::SyncClient query_service = ConnectToQueryService();
    auto call_result = query_service.GetInfo(fuchsia_fs::FilesystemInfoQuery::kMask);
    ASSERT_EQ(call_result.status(), ZX_OK);
    const auto& query_result = call_result.value().result;
    ASSERT_TRUE(query_result.is_response());

    const fuchsia_fs::FilesystemInfo& info = query_result.response().info;

    // Check that total_bytes are a multiple of slice size.
    const uint64_t slice_size = fs().options().fvm_slice_size;
    EXPECT_GE(info.total_bytes(), slice_size);
    EXPECT_EQ(info.total_bytes() % slice_size, 0ul);

    // Check that used_bytes are within a reasonable range.
    EXPECT_GE(info.used_bytes(), expected_bytes);
    EXPECT_LE(info.used_bytes(), info.total_bytes());

    EXPECT_EQ(info.total_nodes(), slice_size / kBlobfsInodeSize);
    EXPECT_EQ(info.used_nodes(), expected_nodes);

    // Should be able to query for the koid of the |fs_id| event.
    EXPECT_TRUE(info.fs_id().is_valid());
    zx_info_handle_basic_t event_info;
    EXPECT_EQ(info.fs_id().get_info(ZX_INFO_HANDLE_BASIC, &event_info, sizeof(event_info), nullptr,
                                    nullptr),
              ZX_OK);
    EXPECT_GE(event_info.koid, 0ul);

    EXPECT_EQ(info.block_size(), kBlobfsBlockSize);
    EXPECT_EQ(info.max_node_name_size(), digest::kSha256HexLength);
    EXPECT_EQ(info.fs_type(), fuchsia_fs::FsType::BLOBFS);

    const std::string expected_fs_name = "blobfs";
    ASSERT_EQ(expected_fs_name.size(), info.name().size());
    ASSERT_EQ(std::string(info.name().data(), info.name().size()), expected_fs_name)
        << "Unexpected filesystem mounted";

    const std::string expected_device_path = fs::GetTopologicalPath(fs().DevicePath().value());
    ASSERT_EQ(expected_device_path.size(), info.device_path().size());
    ASSERT_EQ(std::string(info.device_path().data(), info.device_path().size()),
              expected_device_path)
        << "Incorrect device path";
  }
};

TEST_F(QueryServiceTest, QueryInfo) {
  size_t total_bytes = 0;
  ASSERT_NO_FATAL_FAILURE(QueryInfo(0, 0));
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs().mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
    total_bytes += fbl::round_up(info->size_merkle + info->size_data, kBlobfsBlockSize);
  }

  ASSERT_NO_FATAL_FAILURE(QueryInfo(6, total_bytes));
}

TEST_F(QueryServiceTest, SelectiveQueryInfoEmpty) {
  fuchsia_fs::Query::SyncClient query_service = ConnectToQueryService();
  auto call_result = query_service.GetInfo(fuchsia_fs::FilesystemInfoQuery());
  ASSERT_EQ(call_result.status(), ZX_OK);
  const auto& query_result = call_result.value().result;
  ASSERT_TRUE(query_result.is_response());
  ASSERT_TRUE(query_result.response().info.IsEmpty());
}

TEST_F(QueryServiceTest, SelectiveQueryInfoSingleField) {
  fuchsia_fs::Query::SyncClient query_service = ConnectToQueryService();
  auto call_result = query_service.GetInfo(fuchsia_fs::FilesystemInfoQuery::TOTAL_BYTES);
  ASSERT_EQ(call_result.status(), ZX_OK);
  const auto& query_result = call_result.value().result;
  ASSERT_TRUE(query_result.is_response());
  const fuchsia_fs::FilesystemInfo& info = query_result.response().info;

  ASSERT_FALSE(info.IsEmpty());
  ASSERT_TRUE(info.has_total_bytes());
  ASSERT_FALSE(info.has_used_bytes());
  ASSERT_FALSE(info.has_total_nodes());
  ASSERT_FALSE(info.has_used_nodes());
  ASSERT_FALSE(info.has_fs_id());
  ASSERT_FALSE(info.has_block_size());
  ASSERT_FALSE(info.has_max_node_name_size());
  ASSERT_FALSE(info.has_fs_type());
  ASSERT_FALSE(info.has_name());
  ASSERT_FALSE(info.has_device_path());
}

TEST_F(QueryServiceTest, IsNodeInFilesystemPositiveCase) {
  // Get a token corresponding to the root directory.
  fdio_cpp::UnownedFdioCaller caller(root_fd());
  auto token_result = fio::Directory::Call::GetToken(caller.channel());
  ASSERT_EQ(token_result.status(), ZX_OK);
  ASSERT_EQ(token_result->s, ZX_OK);
  zx::handle token_raw = std::move(token_result->token);
  ASSERT_TRUE(token_raw.is_valid());
  zx::event token(std::move(token_raw));

  // This token is in the filesystem.
  fuchsia_fs::Query::SyncClient query_service = ConnectToQueryService();
  auto result = query_service.IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_TRUE(result->is_in_filesystem);
}

TEST_F(QueryServiceTest, IsNodeInFilesystemNegativeCase) {
  // Create some arbitrary event, to fake a token.
  zx::event token;
  zx::event::create(0, &token);

  // This token should not be in the filesystem.
  fuchsia_fs::Query::SyncClient query_service = ConnectToQueryService();
  auto result = query_service.IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_FALSE(result->is_in_filesystem);
}

}  // namespace
}  // namespace blobfs
