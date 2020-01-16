// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"

#include <lib/gtest/real_loop_fixture.h>

#include <gtest/gtest.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/modular/lib/pseudo_dir/pseudo_dir_utils.h>

namespace modular {
namespace {

class PseudoDirServerTest : public gtest::RealLoopFixture {};

// Test that |PseudoDirServer::OpenAt| gives you an FD can be used on the same
// thread as |PseudoDirServer|.
TEST_F(PseudoDirServerTest, OpenAt) {
  constexpr char kContents[] = "file contents";
  modular::PseudoDirServer server(MakeFilePathWithContents("a/b/c", kContents));

  {
    // Paths with leading '/' don't work.
    fbl::unique_fd fd = server.OpenAt("/a");
    EXPECT_FALSE(fd.is_valid());
  }
  {
    // 'x' doesn't exist, so not valid.
    fbl::unique_fd fd = server.OpenAt("x");
    EXPECT_FALSE(fd.is_valid());
  }
  {
    fbl::unique_fd fd = server.OpenAt("a");
    EXPECT_TRUE(fd.is_valid());
  }
  {
    fbl::unique_fd fd = server.OpenAt("a/b");
    EXPECT_TRUE(fd.is_valid());
  }
  {
    fbl::unique_fd fd = server.OpenAt("a/b/c");
    EXPECT_TRUE(fd.is_valid());

    std::string contents;
    ASSERT_TRUE(files::ReadFileDescriptorToString(fd.get(), &contents));
    EXPECT_EQ(kContents, contents);
  }
}

// Test that |PseudoDirServer::Serve| serves a directory which doesn't block the
// current thread. Test this by using thread-blocking POSIX apis.
TEST_F(PseudoDirServerTest, Serve) {
  constexpr char kFileName[] = "file_name";
  constexpr char kContents[] = "file contents";
  modular::PseudoDirServer server(MakeFilePathWithContents(kFileName, kContents));
  auto dir_fd = fsl::OpenChannelAsFileDescriptor(server.Serve().Unbind().TakeChannel());

  std::string contents;
  ASSERT_TRUE(files::ReadFileToStringAt(dir_fd.get(), kFileName, &contents));
  EXPECT_EQ(kContents, contents);
}

}  // namespace
}  // namespace modular
