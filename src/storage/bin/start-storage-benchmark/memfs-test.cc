// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/memfs.h"

#include <fcntl.h>
#include <lib/fdio/fd.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace storage_benchmark {
namespace {

TEST(MemfsTest, MemfsCanBeCreatedAndUsed) {
  constexpr char kFileName[] = "file";
  constexpr std::string_view kFileContents = "file-contents";
  constexpr ssize_t kFileSize = kFileContents.size();

  auto memfs = Memfs::Create();
  ASSERT_OK(memfs.status_value());

  auto root = memfs->GetFilesystemRoot();
  ASSERT_OK(root.status_value());

  fbl::unique_fd dir;
  ASSERT_OK(fdio_fd_create(root->TakeChannel().release(), dir.reset_and_get_address()));

  fbl::unique_fd file(openat(dir.get(), kFileName, O_CREAT | O_RDWR));
  ASSERT_EQ(pwrite(file.get(), kFileContents.data(), kFileSize, 0), kFileSize);
  std::string contents(kFileSize, 0);
  ASSERT_EQ(pread(file.get(), contents.data(), kFileSize, 0), kFileSize);
  EXPECT_EQ(contents, kFileContents);
}

}  // namespace
}  // namespace storage_benchmark
