// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/allowlist.h"

#include <unistd.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"

namespace component {
namespace {

class AllowlistTest : public ::testing::Test {
 protected:
  std::string NewFile(const std::string& dir, const std::string& contents) {
    const std::string file = fxl::Substitute("$0/file$1", dir, std::to_string(unique_id_++));
    if (!files::WriteFile(file, contents.data(), contents.size())) {
      return "";
    }
    return file;
  }

  files::ScopedTempDir tmp_dir_;

 private:
  int unique_id_ = 1;
};

TEST_F(AllowlistTest, Parse) {
  static constexpr char kFile[] = R"F(
  test_one
  test_two)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  auto filename = NewFile(dir, kFile);
  Allowlist allowlist(filename);
  EXPECT_TRUE(allowlist.IsAllowed("test_one"));
  EXPECT_TRUE(allowlist.IsAllowed("test_two"));
  EXPECT_FALSE(allowlist.IsAllowed(""));
  EXPECT_FALSE(allowlist.IsAllowed("other"));
}

TEST_F(AllowlistTest, MissingFile) {
  Allowlist allowlist("/does/not/exist");
  EXPECT_FALSE(allowlist.IsAllowed("test_one"));
  EXPECT_FALSE(allowlist.IsAllowed("test_two"));
  EXPECT_FALSE(allowlist.IsAllowed(""));
  EXPECT_FALSE(allowlist.IsAllowed("other"));
}

}  // namespace
}  // namespace component
