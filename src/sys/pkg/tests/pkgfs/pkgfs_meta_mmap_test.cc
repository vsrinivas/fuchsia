// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>

#include <fbl/algorithm.h>
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

TEST(PkgfsMetaMmap, MapDifferentSizes) {
  struct TestCase {
    std::string filename;
    size_t size;
  };

  std::vector<TestCase> cases = {
      {"zero_byte_file", 0},    {"one_byte_file", 1},     {"4095_byte_file", 4095},
      {"4096_byte_file", 4096}, {"4097_byte_file", 4097},
  };

  for (const auto& test_case : cases) {
    std::string path = "/pkg/meta/" + test_case.filename;
    fbl::unique_fd fd(open(path.c_str(), O_RDONLY));
    ASSERT_TRUE(fd.is_valid()) << "Could not open file \"" << path << "\" error "
                               << strerror(errno);

    struct stat stat_buffer;

    ASSERT_EQ(fstat(fd.get(), &stat_buffer), 0) << strerror(errno);

    size_t size = stat_buffer.st_size;

    EXPECT_EQ(size, test_case.size) << "for test file " << test_case.filename;

    if (size == 0u) {
      continue;
    }
    void* addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd.get(), 0u);
    ASSERT_NE(addr, MAP_FAILED) << strerror(errno);

    size_t size_rounded_up_to_page_boundary = fbl::round_up(size, 4096u);
    std::string_view mapped_view(static_cast<const char*>(addr), size_rounded_up_to_page_boundary);

    // The file should contain the string "ABCD" repeated until the end of the logical file size.
    // The memory mapping should then contain zeros up until the end of the page.
    for (size_t i = 0; i < size_rounded_up_to_page_boundary; ++i) {
      if (i < size) {
        EXPECT_EQ(mapped_view[i], "ABCD"[i % 4]);
      } else {
        EXPECT_EQ(mapped_view[i], '\0');
      }
    }

    ASSERT_EQ(munmap(addr, size), 0) << strerror(errno);
  }
}
