// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/zx/vmo.h>
#include <zircon/device/vfs.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/fvm/format.h"
#include "src/storage/lib/utils/topological_path.h"

namespace blobfs {
namespace {

namespace fio = fuchsia_io;
namespace fuchsia_fs = fuchsia_fs;

class QueryServiceTest : public BlobfsWithFvmTest {
 protected:
  fidl::WireSyncClient<fuchsia_fs::Query> ConnectToQueryService() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_fs::Query>();
    EXPECT_EQ(endpoints.status_value(), ZX_OK);
    auto [query_client_end, query_server_end] = *std::move(endpoints);

    std::string query_service_path =
        std::string("svc/") + fidl::DiscoverableProtocolName<fuchsia_fs::Query>;
    EXPECT_EQ(
        fdio_service_connect_at(fs().GetOutgoingDirectory()->get(), query_service_path.c_str(),
                                query_server_end.TakeChannel().release()),
        ZX_OK);
    return fidl::WireSyncClient<fuchsia_fs::Query>(std::move(query_client_end));
  }

  void QueryInfo(size_t expected_nodes, size_t expected_bytes) {
    fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
    auto call_result = query_service->GetInfo();
    ASSERT_EQ(call_result.status(), ZX_OK);
    const auto& query_result = call_result.value().result;
    ASSERT_TRUE(query_result.is_response());

    const fuchsia_io_admin::wire::FilesystemInfo& info = query_result.response().info;

    // Check that total_bytes are a multiple of slice size.
    const uint64_t slice_size = fs().options().fvm_slice_size;
    EXPECT_GE(info.total_bytes, slice_size);
    EXPECT_EQ(info.total_bytes % slice_size, 0ul);

    // Check that used_bytes are within a reasonable range.
    EXPECT_GE(info.used_bytes, expected_bytes);
    EXPECT_LE(info.used_bytes, info.total_bytes);

    EXPECT_GE(info.total_nodes, expected_nodes);
    EXPECT_EQ((info.total_nodes * sizeof(Inode)) % slice_size, 0ul);
    EXPECT_EQ(info.used_nodes, expected_nodes);
    EXPECT_NE(info.fs_id, 0u);  // Just validate a nonzero ID value.
    EXPECT_EQ(info.block_size, kBlobfsBlockSize);
    EXPECT_EQ(info.max_filename_size, digest::kSha256HexLength);
    EXPECT_EQ(info.fs_type, VFS_TYPE_BLOBFS);

    ASSERT_EQ(reinterpret_cast<const char*>(info.name.data()), std::string("blobfs"));
  }
};

TEST_F(QueryServiceTest, QueryInfo) {
  size_t total_bytes = 0;
  ASSERT_NO_FATAL_FAILURE(QueryInfo(0, 0));
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);
    std::unique_ptr<MerkleTreeInfo> merkle_tree =
        CreateMerkleTree(info->data.get(), info->size_data, /*use_compact_format=*/true);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
    total_bytes += fbl::round_up(merkle_tree->merkle_tree_size + info->size_data, kBlobfsBlockSize);
  }

  ASSERT_NO_FATAL_FAILURE(QueryInfo(6, total_bytes));
}

TEST_F(QueryServiceTest, IsNodeInFilesystemPositiveCase) {
  // Get a token corresponding to the root directory.
  fdio_cpp::UnownedFdioCaller caller(root_fd());
  auto token_result =
      fidl::WireCall(fidl::UnownedClientEnd<fio::Directory>(caller.channel()))->GetToken();
  ASSERT_EQ(token_result.status(), ZX_OK);
  ASSERT_EQ(token_result->s, ZX_OK);
  zx::handle token_raw = std::move(token_result->token);
  ASSERT_TRUE(token_raw.is_valid());
  zx::event token(std::move(token_raw));

  // This token is in the filesystem.
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto result = query_service->IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_TRUE(result->is_in_filesystem);
}

TEST_F(QueryServiceTest, IsNodeInFilesystemNegativeCase) {
  // Create some arbitrary event, to fake a token.
  zx::event token;
  zx::event::create(0, &token);

  // This token should not be in the filesystem.
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto result = query_service->IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_FALSE(result->is_in_filesystem);
}

}  // namespace
}  // namespace blobfs
