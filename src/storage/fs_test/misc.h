// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_MISC_H_
#define SRC_STORAGE_FS_TEST_MISC_H_

#include <dirent.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <string_view>

#include <fbl/span.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {

struct ExpectedDirectoryEntry {
  std::string_view name;
  unsigned char d_type;  // Same as the d_type entry from struct dirent.
};

void CheckDirectoryContents(DIR* dir, fbl::Span<const ExpectedDirectoryEntry> entries);
void CheckDirectoryContents(const char* dirname, fbl::Span<const ExpectedDirectoryEntry> entries);

// Checks the contents of a file are what we expect.
void CheckFileContents(int fd, fbl::Span<const uint8_t> expected);

// Checks that it's possible to create a directory with the given name.
void CheckCanCreateDirectory(FilesystemTest* test, const char* name, bool do_delete);

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_MISC_H_
