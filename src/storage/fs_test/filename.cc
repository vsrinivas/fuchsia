// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include <fbl/unique_fd.h>

#include "gtest/gtest.h"
#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using FilenameTest = FilesystemTest;
TEST_P(FilenameTest, TestOnlySpacePeriodNameSucceeds) {
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "  .  ", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "  . ", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, ".  . ", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, ".  . .", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, ".....", false));
  ASSERT_NO_FATAL_FAILURE(CheckCanCreateDirectory(this, "     ", false));
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, FilenameTest, testing::ValuesIn(AllTestFilesystems()),
    testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
