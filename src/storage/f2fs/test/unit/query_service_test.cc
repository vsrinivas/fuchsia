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

#include <gtest/gtest.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/lib/utils/topological_path.h"
#include "third_party/f2fs/f2fs.h"
#include "third_party/f2fs/f2fs_layout.h"
#include "third_party/f2fs/f2fs_types.h"

namespace f2fs {
namespace {

namespace fio = fuchsia_io;
namespace fuchsia_fs = fuchsia_fs;

using fuchsia_fs::wire::FilesystemInfoQuery;

class QueryServiceTest : public fs_test::FilesystemTest {
 public:
  int root_fd() {
    if (!root_fd_) {
      root_fd_.reset(open(fs().mount_path().c_str(), O_DIRECTORY));
    }
    return root_fd_.get();
  }

 private:
  fbl::unique_fd root_fd_;

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
    auto call_result = query_service.GetInfo(FilesystemInfoQuery::kMask);
    ASSERT_EQ(call_result.status(), ZX_OK);
    const auto& query_result = call_result.value().result;
    ASSERT_TRUE(query_result.is_response());

    const fuchsia_fs::wire::FilesystemInfo& info = query_result.response().info;

    // Check that total_bytes are a multiple of slice size.
    const uint64_t slice_size = fs().options().fvm_slice_size;
    EXPECT_GE(info.total_bytes(), slice_size);
    EXPECT_EQ(info.total_bytes() % slice_size, 0ul);

    // Check that used_bytes are within a reasonable range.
    EXPECT_GE(info.used_bytes(), expected_bytes);
    EXPECT_LE(info.used_bytes(), info.total_bytes());

    EXPECT_GE(info.total_nodes(), expected_nodes);
    EXPECT_EQ((info.total_nodes() * (sizeof(Inode) + sizeof(NodeFooter))) % slice_size, 0ul);
    EXPECT_EQ(info.used_nodes(), expected_nodes);

    // Should be able to query for the koid of the |fs_id| event.
    EXPECT_TRUE(info.fs_id().is_valid());
    zx_info_handle_basic_t event_info;
    EXPECT_EQ(info.fs_id().get_info(ZX_INFO_HANDLE_BASIC, &event_info, sizeof(event_info), nullptr,
                                    nullptr),
              ZX_OK);
    EXPECT_GE(event_info.koid, 0ul);

    EXPECT_EQ(info.block_size(), kBlockSize);
    EXPECT_EQ(info.max_node_name_size(), kMaxNameLen);
    EXPECT_EQ(info.fs_type(), fuchsia_fs::wire::FsType::kF2Fs);

    const std::string expected_fs_name = "f2fs";
    ASSERT_EQ(expected_fs_name.size(), info.name().size());
    ASSERT_EQ(std::string(info.name().data(), info.name().size()), expected_fs_name)
        << "Unexpected filesystem mounted";

    const std::string expected_device_path =
        storage::GetTopologicalPath(fs().DevicePath().value()).value();
    ASSERT_EQ(expected_device_path.size(), info.device_path().size());
    ASSERT_EQ(std::string(info.device_path().data(), info.device_path().size()),
              expected_device_path)
        << "Incorrect device path";
  }
};

TEST_P(QueryServiceTest, QueryInfo) {
  size_t total_bytes = 0;
  ASSERT_NO_FATAL_FAILURE(QueryInfo(1, 0));

  const uint64_t kExtraNodeCount = 16;
  for (uint64_t i = 0; i < kExtraNodeCount; i++) {
    const std::string path = GetPath("file_" + std::to_string(i));

    fbl::unique_fd fd(open(path.c_str(), O_CREAT | O_RDWR));
    ASSERT_GT(fd.get(), 0);
    total_bytes += kBlockSize;
  }

  ASSERT_NO_FATAL_FAILURE(QueryInfo(1 + kExtraNodeCount, total_bytes));
}

TEST_P(QueryServiceTest, SelectiveQueryInfoEmpty) {
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto call_result = query_service.GetInfo(FilesystemInfoQuery());
  ASSERT_EQ(call_result.status(), ZX_OK);
  const auto& query_result = call_result.value().result;
  ASSERT_TRUE(query_result.is_response());
  ASSERT_TRUE(query_result.response().info.IsEmpty());
}

TEST_P(QueryServiceTest, SelectiveQueryInfoSingleField) {
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto call_result = query_service.GetInfo(FilesystemInfoQuery::kTotalBytes);
  ASSERT_EQ(call_result.status(), ZX_OK);
  const auto& query_result = call_result.value().result;
  ASSERT_TRUE(query_result.is_response());
  const fuchsia_fs::wire::FilesystemInfo& info = query_result.response().info;

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

TEST_P(QueryServiceTest, IsNodeInFilesystemPositiveCase) {
  // Get a token corresponding to the root directory.
  fdio_cpp::UnownedFdioCaller caller(root_fd());
  auto token_result =
      fidl::WireCall(fidl::UnownedClientEnd<fio::Directory>(caller.channel())).GetToken();
  ASSERT_EQ(token_result.status(), ZX_OK);
  ASSERT_EQ(token_result->s, ZX_OK);
  zx::handle token_raw = std::move(token_result->token);
  ASSERT_TRUE(token_raw.is_valid());
  zx::event token(std::move(token_raw));

  // This token is in the filesystem.
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto result = query_service.IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_TRUE(result->is_in_filesystem);
}

TEST_P(QueryServiceTest, IsNodeInFilesystemNegativeCase) {
  // Create some arbitrary event, to fake a token.
  zx::event token;
  zx::event::create(0, &token);

  // This token should not be in the filesystem.
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto result = query_service.IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_FALSE(result->is_in_filesystem);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, QueryServiceTest,
                         testing::ValuesIn(fs_test::AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace f2fs
