// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/service/llcpp/service.h>
#include <unistd.h>

#include <numeric>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using QueryFilesystemTest = FilesystemTest;

TEST_P(QueryFilesystemTest, QueryTest) {
  // First use DirectoryAdmin to get filesystem info.

  fbl::unique_fd root_fd(open(fs().mount_path().c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(root_fd);
  fdio_cpp::UnownedFdioCaller root_connection(root_fd);
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::DirectoryAdmin>(
                                   zx::unowned_channel(root_connection.borrow_channel())))
                    .QueryFilesystem();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->s, ZX_OK);
  const auto& info = *result->info;

  // Some very basic sanity checks.
  EXPECT_TRUE(info.total_bytes >= info.used_bytes);
  EXPECT_GE(info.block_size, 512u);
  EXPECT_TRUE(info.max_filename_size > 32);

  fidl::ClientEnd<fuchsia_fs::Query> fs_query;

  if (fs().GetTraits().supports_fs_query) {
    auto svc = fs().GetSvcDirectory();
    auto client_end_or = service::ConnectAt<fuchsia_fs::Query>(svc);
    ASSERT_FALSE(client_end_or.is_error());
    fs_query = std::move(client_end_or).value();

    auto result = fidl::WireCall(fs_query).GetInfo(fuchsia_fs::wire::FilesystemInfoQuery::kMask);
    ASSERT_TRUE(result.ok() && result->result.is_response());
    const auto& info2 = result->result.response().info;

    EXPECT_EQ(info2.total_bytes(), info.total_bytes);
    EXPECT_EQ(info2.used_bytes(), info.used_bytes);
    EXPECT_EQ(info2.block_size(), info.block_size);
    EXPECT_EQ(info2.max_node_name_size(), info.max_filename_size);
    EXPECT_EQ(static_cast<uint32_t>(info2.fs_type()), info.fs_type);
    // TODO(csuter): Add support for other members
  }

  // Create a file and write to it, and it should increase used_bytes.
  fbl::unique_fd fd(open(GetPath("query-test").c_str(), O_CREAT | O_RDWR, 0666));
  ASSERT_TRUE(fd);
  const size_t buf_size = info.block_size * 7;
  auto buf = std::make_unique<uint8_t[]>(buf_size);
  std::iota(&buf[0], &buf[buf_size], 0);
  EXPECT_EQ(write(fd.get(), buf.get(), buf_size), static_cast<ssize_t>(buf_size));

  auto result2 = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::DirectoryAdmin>(
                                    zx::unowned_channel(root_connection.borrow_channel())))
                     .QueryFilesystem();
  ASSERT_TRUE(result2.ok() && result2->s == ZX_OK);
  const auto& info2 = *result2->info;

  // There should be no change in most of the values.
  if (fs().options().use_fvm) {
    // If using FVM, then the amount of bytes might have incresased.
    EXPECT_GE(info2.total_bytes, info.total_bytes);
  } else {
    EXPECT_EQ(info2.total_bytes, info.total_bytes);
  }
  EXPECT_EQ(info2.block_size, info.block_size);
  EXPECT_EQ(info2.max_filename_size, info.max_filename_size);

  // Used bytes should have increased by at *least* buf_size.
  if (!fs().GetTraits().in_memory) {
    EXPECT_GE(info2.used_bytes, info.used_bytes + buf_size);
  }

  if (fs().GetTraits().supports_fs_query) {
    auto result2 = fidl::WireCall(fs_query).GetInfo(fuchsia_fs::wire::FilesystemInfoQuery::kMask);
    ASSERT_TRUE(result2.ok() && result2->result.is_response());
    const auto& info3 = result2->result.response().info;

    EXPECT_EQ(info3.total_bytes(), info2.total_bytes);
    EXPECT_EQ(info3.used_bytes(), info2.used_bytes);
    EXPECT_EQ(info3.block_size(), info2.block_size);
    EXPECT_EQ(info3.max_node_name_size(), info2.max_filename_size);
    EXPECT_EQ(static_cast<uint32_t>(info3.fs_type()), info2.fs_type);
  }
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, QueryFilesystemTest,
                         testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
