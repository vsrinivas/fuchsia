// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/stdio.h>

#include "tests.h"

namespace {

struct FileIo {
  using storage_type = FILE*;

  void Create(fbl::unique_fd fd, size_t size, FILE** zbi) {
    ASSERT_TRUE(fd);
    *zbi = fdopen(fd.release(), "r+");
  }

  void ReadPayload(FILE* zbi, const zbi_header_t& header, long int payload, std::string* string) {
    string->resize(header.length);
    ASSERT_EQ(0, fseek(zbi, payload, SEEK_SET), "failed to seek to payload: %s", strerror(errno));
    size_t n = fread(string->data(), 1, header.length, zbi);
    ASSERT_EQ(0, ferror(zbi), "failed to read payload: %s", strerror(errno));
    ASSERT_EQ(header.length, n, "did not fully read payload");
  }
};

// The type of FILE* cannot be default-constructed, so we skip the
// TestDefaultConstructedView() test case.

TEST_ITERATIONS(ZbitlViewStdioTests, FileIo)

TEST_MUTATIONS(ZbitlViewStdioTests, FileIo)

TEST(ZbitlViewStdioTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<FileIo>());
}

}  // namespace
