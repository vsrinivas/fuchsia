// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TEST_POSIX_TESTS_H_
#define FS_TEST_POSIX_TESTS_H_

#include <fs/test_support/fixtures.h>
namespace posix_tests {

using fs::FilesystemTest;

enum CloseUnlinkOrder {
  UnlinkThenClose,  // Unlink the file while file is still open.
  CloseThenUnlink,  // Close the file before unlinking it.
};

enum TestType {
  KeepOpen,  // Truncates while file is still open.
  Reopen,    // Unused.
  Remount,   // Remounts filesystem after truncate but before writing to it.
};

// Test that truncate doesn't have issues dealing with files smaller than a
// block size.
void TestTruncateSingleBlockFile(FilesystemTest* ops);

// Test that truncate doesn't have issues dealing with larger files.
// Repeatedly write to / truncate a file.
void TestTruncateMultiBlockFile(FilesystemTest* ops, size_t buf_size, size_t iterations,
                                TestType type);

// This test catches a particular regression in truncation, where,
// if a block is cut in half for truncation, it is read, filled with
// zeroes, and written back out to disk.
//
// This test tries to probe at a variety of offsets of interest.
void TestTruncatePartialBlockSparse(FilesystemTest* ops, CloseUnlinkOrder order);

// Creates a sparse file with multiple holes and truncates the file in the hole.
void TestTruncatePartialBlockSparse(FilesystemTest* ops, CloseUnlinkOrder order);

// Tests truncate error conditions like
//    - truncate to negative size.
//    - truncate to extremely large file.
void TestTruncateErrno(FilesystemTest* ops);

}  // namespace posix_tests

#endif  // FS_TEST_POSIX_TESTS_H_
