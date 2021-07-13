// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/namespace_init.h"

#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

class NamespaceInitTest : public ::testing::Test {
 public:
  std::string RootdDir() { return temp_dir_.path(); }

  void WriteFile(const std::string& path, const std::string& content) {
    FX_CHECK(files::WriteFile(path, content)) << "Failed to write to " << path;
  }

  std::string ReadFile(const std::string& path) {
    std::string content;
    FX_CHECK(files::ReadFileToString(path, &content)) << "Failed to read from " << path;
    return content;
  }

 private:
  files::ScopedTempDir temp_dir_;
};

TEST_F(NamespaceInitTest, TestAndSetNotAFdr) {
  std::string path = files::JoinPath(RootdDir(), "not_a_fdr.txt");

  EXPECT_FALSE(TestAndSetNotAFdr(path));

  EXPECT_TRUE(TestAndSetNotAFdr(path));
  EXPECT_TRUE(TestAndSetNotAFdr(path));

  path = "/bad_path/not_a_fdr.txt";

  EXPECT_FALSE(TestAndSetNotAFdr(path));

  EXPECT_FALSE(TestAndSetNotAFdr(path));
  EXPECT_FALSE(TestAndSetNotAFdr(path));
}

TEST_F(NamespaceInitTest, MovePreviousRebootReason) {
  const std::string to = files::JoinPath(RootdDir(), "to.txt");
  const std::string from = files::JoinPath(RootdDir(), "from.txt");

  // |from| doesn't exist.
  MovePreviousRebootReason(from, to);
  EXPECT_FALSE(files::IsFile(to));

  // |to| can't be written to.
  WriteFile(from, "reboot_reason");
  MovePreviousRebootReason("/bad_path/to.txt", from);
  EXPECT_FALSE(files::IsFile("/bad_path/to.txt"));
  EXPECT_TRUE(files::IsFile(from));
  EXPECT_EQ(ReadFile(from), "reboot_reason");

  // Everything works!
  WriteFile(from, "reboot_reason");
  MovePreviousRebootReason(from, to);
  EXPECT_FALSE(files::IsFile(from));
  EXPECT_TRUE(files::IsFile(to));
  EXPECT_EQ(ReadFile(to), "reboot_reason");
}

}  // namespace
}  // namespace forensics::feedback
