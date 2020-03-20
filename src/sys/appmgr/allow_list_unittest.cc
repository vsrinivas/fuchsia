// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/allow_list.h"

#include <fcntl.h>
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

class AllowListTest : public ::testing::Test {
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

TEST_F(AllowListTest, Parse) {
  static constexpr char kFile[] = R"F(
  test_one
  test_two)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(allowlist.IsAllowed("test_one"));
  EXPECT_TRUE(allowlist.IsAllowed("test_two"));
  EXPECT_FALSE(allowlist.IsAllowed(""));
  EXPECT_FALSE(allowlist.IsAllowed("other"));
}

TEST_F(AllowListTest, MissingFile) {
  fxl::UniqueFD dirfd(open(".", O_RDONLY));
  AllowList allowlist(dirfd, "/does/not/exist");
  EXPECT_FALSE(allowlist.IsAllowed("test_one"));
  EXPECT_FALSE(allowlist.IsAllowed("test_two"));
  EXPECT_FALSE(allowlist.IsAllowed(""));
  EXPECT_FALSE(allowlist.IsAllowed("other"));
}

TEST_F(AllowListTest, ParsePackageUrls) {
  static constexpr char kFile[] = R"F(
    fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx
    fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(allowlist.IsAllowed("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx"));
  EXPECT_TRUE(allowlist.IsAllowed("fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx"));
  EXPECT_FALSE(allowlist.IsAllowed(""));
  EXPECT_FALSE(allowlist.IsAllowed("fuchsia-pkg://fuchsia.com/baz#meta/baz.cmx"));
  EXPECT_FALSE(allowlist.IsAllowed("fuchsia-pkg://fuchsia.com"));
  EXPECT_FALSE(allowlist.IsAllowed("fuchsia-pkg://"));
}

TEST_F(AllowListTest, WildcardAllow) {
  static constexpr char kFile[] = R"F(
  # Some comment about why we allow everything in this build
  *)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(allowlist.IsAllowed("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx"));
  EXPECT_TRUE(allowlist.IsAllowed("fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx"));
  EXPECT_TRUE(allowlist.IsAllowed("literally-anything-at-all"));
  EXPECT_TRUE(allowlist.IsAllowed(""));
}

TEST_F(AllowListTest, CommentsAreOmitted) {
  static constexpr char kFile[] = R"F(
    test_one
    # foo
    test_two
    #foo_bar
    File#Name
    FileName#)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(allowlist.IsAllowed("test_one"));
  EXPECT_TRUE(allowlist.IsAllowed("test_two"));
  EXPECT_TRUE(allowlist.IsAllowed("File#Name"));
  EXPECT_TRUE(allowlist.IsAllowed("FileName#"));
  EXPECT_FALSE(allowlist.IsAllowed(""));
  EXPECT_FALSE(allowlist.IsAllowed("other"));
  EXPECT_FALSE(allowlist.IsAllowed("# foo"));
  EXPECT_FALSE(allowlist.IsAllowed("#foo_bar"));
}

}  // namespace
}  // namespace component
