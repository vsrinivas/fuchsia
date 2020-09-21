// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <fs/test/posix/tests.h>
#include <zxtest/zxtest.h>

#include "minfs_fixtures.h"
#include "utils.h"

namespace {

using fs::FilesystemTest;

using TruncateTest = MinfsTest;
using TruncateTestWithFvm = MinfsTestWithFvm;

constexpr size_t kTinyFileSize = 1 << 10;
constexpr size_t kSmallFileSize = 1 << 15;
// With kFewIterations or with kManyIterations, large tests are timing out.
// The tests need to be modified to spend less time doing IO or comparing
// results. We need to fix this and delete kTenIterations.
// TODO: 44323
// constexpr size_t kMediumFileSize = 1 << 20;
// constexpr size_t kLargeFileSize = 1 << 25;
//
// constexpr size_t kTenIterations = 10;
constexpr size_t kFewIterations = 50;
constexpr size_t kManyIterations = 100;

TEST_F(TruncateTest, TruncateSingleBlockFile) { posix_tests::TestTruncateSingleBlockFile(this); }

TEST_F(TruncateTest, TruncateTinyFile) {
  posix_tests::TestTruncateMultiBlockFile(this, kTinyFileSize, kManyIterations,
                                          posix_tests::TestType::KeepOpen);
}

TEST_F(TruncateTest, TruncateTinyFileWithReopen) {
  posix_tests::TestTruncateMultiBlockFile(this, kTinyFileSize, kManyIterations,
                                          posix_tests::TestType::Reopen);
}

TEST_F(TruncateTest, TruncateSmallFile) {
  posix_tests::TestTruncateMultiBlockFile(this, kSmallFileSize, kFewIterations,
                                          posix_tests::TestType::KeepOpen);
}

TEST_F(TruncateTest, TruncateSmallFileWithReopen) {
  posix_tests::TestTruncateMultiBlockFile(this, kSmallFileSize, kFewIterations,
                                          posix_tests::TestType::Reopen);
}

// These tests are disabled because they are taking too long to run and this
// is making tests time out. Disabling these tests for now.
// TODO: 44323
// TEST_F(TruncateTest, TruncateMediumFile) {
//   posix_tests::TestTruncateMultiBlockFile(this, kMediumFileSize, kFewIterations,
//                                           posix_tests::TestType::KeepOpen);
// }
//
// TEST_F(TruncateTest, TruncateMediumFileWithReopen) {
//   posix_tests::TestTruncateMultiBlockFile(this, kMediumFileSize, kFewIterations,
//                                           posix_tests::TestType::Reopen);
// }
//
// TEST_F(TruncateTest, TruncateMediumFileWithRemount) {
//   posix_tests::TestTruncateMultiBlockFile(this, kMediumFileSize, kFewIterations,
//                                           posix_tests::TestType::Remount);
// }
//
// TEST_F(TruncateTest, TruncateLargeFile) {
//   posix_tests::TestTruncateMultiBlockFile(this, kLargeFileSize, kFewIterations,
//                                           posix_tests::TestType::KeepOpen);
// }
//
// TEST_F(TruncateTest, TruncateLargeFileWithReopen) {
//   posix_tests::TestTruncateMultiBlockFile(this, kLargeFileSize, kFewIterations,
//                                           posix_tests::TestType::Reopen);
// }
//
// TEST_F(TruncateTest, TruncateLargeFileWithRemount) {
//   posix_tests::TestTruncateMultiBlockFile(this, kLargeFileSize, kFewIterations,
//                                           posix_tests::TestType::Remount);
// }

TEST_F(TruncateTest, PartialBlockSparseUnlinkThenClose) {
  posix_tests::TestTruncatePartialBlockSparse(this, posix_tests::CloseUnlinkOrder::UnlinkThenClose);
}

TEST_F(TruncateTest, PartialBlockSparseCloseThenUnlink) {
  posix_tests::TestTruncatePartialBlockSparse(this, posix_tests::CloseUnlinkOrder::CloseThenUnlink);
}

TEST_F(TruncateTest, InvalidArguments) { posix_tests::TestTruncateErrno(this); }

TEST_F(TruncateTestWithFvm, TruncateSingleBlockFile) {
  posix_tests::TestTruncateSingleBlockFile(this);
}

TEST_F(TruncateTestWithFvm, TruncateTinyFile) {
  posix_tests::TestTruncateMultiBlockFile(this, kTinyFileSize, kManyIterations,
                                          posix_tests::TestType::KeepOpen);
}

TEST_F(TruncateTestWithFvm, TruncateTinyFileWithReopen) {
  posix_tests::TestTruncateMultiBlockFile(this, kTinyFileSize, kManyIterations,
                                          posix_tests::TestType::Reopen);
}

TEST_F(TruncateTestWithFvm, TruncateSmallFile) {
  posix_tests::TestTruncateMultiBlockFile(this, kSmallFileSize, kFewIterations,
                                          posix_tests::TestType::KeepOpen);
}

TEST_F(TruncateTestWithFvm, TruncateSmallFileWithReopen) {
  posix_tests::TestTruncateMultiBlockFile(this, kSmallFileSize, kFewIterations,
                                          posix_tests::TestType::Reopen);
}

// These tests are disabled because they are taking too long to run and this
// is making tests time out. Disabling these tests for now.
// TODO: 44323
// TEST_F(TruncateTestWithFvm, TruncateMediumFile) {
//   posix_tests::TestTruncateMultiBlockFile(this, kMediumFileSize, kFewIterations,
//                                           posix_tests::TestType::KeepOpen);
// }
//
// TEST_F(TruncateTestWithFvm, TruncateMediumFileWithReopen) {
//   posix_tests::TestTruncateMultiBlockFile(this, kMediumFileSize, kFewIterations,
//                                           posix_tests::TestType::Reopen);
// }
//
// TEST_F(TruncateTestWithFvm, TruncateMediumFileWithRemount) {
//   posix_tests::TestTruncateMultiBlockFile(this, kMediumFileSize, kFewIterations,
//                                           posix_tests::TestType::Remount);
// }
//
// TEST_F(TruncateTestWithFvm, TruncateLargeFile) {
//   posix_tests::TestTruncateMultiBlockFile(this, kLargeFileSize, kTenIterations,
//                                           posix_tests::TestType::KeepOpen);
// }
//
// TEST_F(TruncateTestWithFvm, TruncateLargeFileWithReopen) {
//   posix_tests::TestTruncateMultiBlockFile(this, kLargeFileSize, kTenIterations,
//                                           posix_tests::TestType::Reopen);
// }
//
// TEST_F(TruncateTestWithFvm, TruncateLargeFileWithRemount) {
//   posix_tests::TestTruncateMultiBlockFile(this, kLargeFileSize, kTenIterations,
//                                           posix_tests::TestType::Remount);
// }
//

TEST_F(TruncateTestWithFvm, PartialBlockSparseUnlinkThenClose) {
  posix_tests::TestTruncatePartialBlockSparse(this, posix_tests::CloseUnlinkOrder::UnlinkThenClose);
}

TEST_F(TruncateTestWithFvm, PartialBlockSparseCloseThenUnlink) {
  posix_tests::TestTruncatePartialBlockSparse(this, posix_tests::CloseUnlinkOrder::CloseThenUnlink);
}

TEST_F(TruncateTestWithFvm, InvalidArguments) { posix_tests::TestTruncateErrno(this); }

}  // namespace
