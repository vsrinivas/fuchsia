// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/component/cpp/service_client.h>
#include <unistd.h>

#include <numeric>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using QueryFilesystemTest = FilesystemTest;

TEST_P(QueryFilesystemTest, QueryTest) {
  const zx::result result1 = fs().GetFsInfo();
  ASSERT_TRUE(result1.is_ok()) << result1.status_string();
  const auto& info1 = result1.value();

  // Some very basic sanity checks.
  EXPECT_TRUE(info1.total_bytes >= info1.used_bytes);
  EXPECT_GE(info1.block_size, 512u);
  EXPECT_TRUE(info1.max_filename_size > 32);

  // Create a file and write to it, and it should increase used_bytes.
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd((open(GetPath("query-test").c_str(), O_CREAT | O_RDWR, 0666))))
      << strerror(errno);
  const size_t buf_size{static_cast<size_t>(info1.block_size) * 7};
  auto buf = std::make_unique<uint8_t[]>(buf_size);
  std::iota(&buf[0], &buf[buf_size], 0);
  EXPECT_EQ(write(fd.get(), buf.get(), buf_size), static_cast<ssize_t>(buf_size))
      << strerror(errno);

  const zx::result result2 = fs().GetFsInfo();
  ASSERT_TRUE(result2.is_ok()) << result2.status_string();
  const auto& info2 = result2.value();

  // There should be no change in most of the values.
  if (fs().options().use_fvm) {
    // If using FVM, then the amount of bytes might have incresased.
    EXPECT_GE(info2.total_bytes, info1.total_bytes);
  } else {
    EXPECT_EQ(info2.total_bytes, info1.total_bytes);
  }
  EXPECT_EQ(info2.block_size, info1.block_size);
  EXPECT_EQ(info2.max_filename_size, info1.max_filename_size);

  // Used bytes should have increased by at *least* buf_size.
  if (!fs().GetTraits().in_memory) {
    EXPECT_GE(info2.used_bytes, info1.used_bytes + buf_size);
  }

  const zx::result result3 = fs().GetFsInfo();
  ASSERT_TRUE(result3.is_ok()) << result3.status_string();
  const auto& info3 = result3.value();

  EXPECT_EQ(info3.total_bytes, info2.total_bytes);
  EXPECT_EQ(info3.used_bytes, info2.used_bytes);
  EXPECT_EQ(info3.block_size, info2.block_size);
  EXPECT_EQ(info3.max_filename_size, info2.max_filename_size);
  EXPECT_EQ(info3.fs_type, info2.fs_type);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, QueryFilesystemTest,
                         testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
