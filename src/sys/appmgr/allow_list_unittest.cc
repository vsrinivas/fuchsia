// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/allow_list.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

  static bool IsAllowed(const AllowList& al, const std::string& url_str) {
    FuchsiaPkgUrl url;
    FX_CHECK(url.Parse(url_str)) << "Invalid URL in test: " << url_str;
    return al.IsAllowed(url);
  }

 private:
  int unique_id_ = 1;
};

TEST_F(AllowListTest, Parse) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/test_one#meta/test_one.cmx
  fuchsia-pkg://fuchsia.com/test_two#meta/test_two.cmx)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fbl::unique_fd dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_one#meta/test_one.cmx"));
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_two#meta/test_two.cmx"));
  EXPECT_FALSE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_one#meta/other.cmx"));
}

TEST_F(AllowListTest, MissingFile) {
  fbl::unique_fd dirfd(open(".", O_RDONLY));
  AllowList allowlist(dirfd, "/does/not/exist");
  EXPECT_FALSE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_one#meta/test_one.cmx"));
  EXPECT_FALSE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_two#meta/test_two.cmx"));
}

TEST_F(AllowListTest, IgnoreVariantAndHash) {
  static constexpr char kFile[] = R"F(
    fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fbl::unique_fd dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/foo/1#meta/foo.cmx"));
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/foo?hash=1234#meta/foo.cmx"));
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/foo/1?hash=1234#meta/foo.cmx"));
}

TEST_F(AllowListTest, WildcardAllow) {
  static constexpr char kFile[] = R"F(
  # Some comment about why we allow everything in this build
  *)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fbl::unique_fd dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx"));
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx"));
}

TEST_F(AllowListTest, CommentsAreIgnored) {
  static constexpr char kFile[] = R"F(
  #foo
  fuchsia-pkg://fuchsia.com/test_one#meta/test_one.cmx
  #bar
  fuchsia-pkg://fuchsia.com/test_two#meta/test_two.cmx
  #fuchsia-pkg://fuchsia.com/test_one#meta/other.cmx)F";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fbl::unique_fd dirfd(open(dir.c_str(), O_RDONLY));
  auto filename = NewFile(dir, kFile);
  AllowList allowlist(dirfd, filename);
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_one#meta/test_one.cmx"));
  EXPECT_TRUE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_two#meta/test_two.cmx"));
  EXPECT_FALSE(IsAllowed(allowlist, "fuchsia-pkg://fuchsia.com/test_one#meta/other.cmx"));
}

}  // namespace
}  // namespace component
