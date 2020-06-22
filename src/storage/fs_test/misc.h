// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_MISC_H_
#define SRC_STORAGE_FS_TEST_MISC_H_

#include <dirent.h>
#include <stdint.h>

#include <string_view>

#include <fbl/span.h>

namespace fs_test {

struct ExpectedDirectoryEntry {
  std::string_view name;
  unsigned char d_type;  // Same as the d_type entry from struct dirent.
};

__EXPORT void CheckDirectoryContents(DIR* dir, fbl::Span<const ExpectedDirectoryEntry> entries);
__EXPORT void CheckDirectoryContents(const char* dirname,
                                     fbl::Span<const ExpectedDirectoryEntry> entries);

// Checks the contents of a file are what we expect.
__EXPORT void CheckFileContents(int fd, fbl::Span<const uint8_t> expected);

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_MISC_H_
