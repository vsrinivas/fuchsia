// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/filesystem/get_directory_content_size.h"

#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/scoped_temp_dir.h>

#include "gtest/gtest.h"

namespace ledger {
namespace {

const std::string kFileContent = "file content";

TEST(GetDirectoryContentSizeTest, GetDirectoryContentSize) {
  files::ScopedTempDir temp_dir;
  ASSERT_TRUE(files::CreateDirectory(temp_dir.path() + "/foo"));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/bar", kFileContent.data(),
                               kFileContent.size()));
  ASSERT_TRUE(files::WriteFile(temp_dir.path() + "/foo/baz",
                               kFileContent.data(), kFileContent.size()));
  uint64_t directory_size = 0;
  ASSERT_TRUE(GetDirectoryContentSize(temp_dir.path(), &directory_size));
  ASSERT_EQ(directory_size, 2 * kFileContent.size());
}

}  // namespace
}  // namespace ledger
