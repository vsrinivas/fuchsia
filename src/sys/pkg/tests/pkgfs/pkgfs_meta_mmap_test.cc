// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"

// TOOD(53247): Port to rust and move to
// src/sys/pkg/lib/fuchsia-pkg-testing/tests/pkgfs_test.rs
TEST(PkgfsMetaMmap, MapRead) {
  fbl::unique_fd fd(open("/pkg/meta/pkgfs_meta_mmap_test.cmx", O_RDONLY));
  ASSERT_TRUE(fd.is_valid()) << "Could not open file" << strerror(errno);

  struct stat stat_buffer;

  ASSERT_EQ(fstat(fd.get(), &stat_buffer), 0) << strerror(errno);

  size_t size = stat_buffer.st_size;

  ASSERT_GT(size, 0u);

  // Read the file contents using read() calls first.
  std::string file_contents;
  ASSERT_TRUE(files::ReadFileDescriptorToString(fd.get(), &file_contents));

  // Sanity check contents
  EXPECT_EQ(file_contents.size(), size);
  EXPECT_NE(file_contents.find("test/pkgfs_meta_mmap_test"), std::string::npos);

  // mmap the file read only and verify contents are the same.

  void* addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd.get(), 0u);
  ASSERT_NE(addr, MAP_FAILED) << strerror(errno);

  std::string_view mapped_view(static_cast<const char*>(addr), size);

  ASSERT_EQ(mapped_view, file_contents);

  ASSERT_EQ(munmap(addr, size), 0) << strerror(errno);
}
