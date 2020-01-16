// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/pseudo_dir/pseudo_dir_utils.h"

#include <lib/gtest/real_loop_fixture.h>

#include <gtest/gtest.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/modular/lib/pseudo_dir/pseudo_dir_server.h>

namespace modular {
namespace {

constexpr int kDefaultBufferSize = 1024;

class PseudoDirUtilsTest : public gtest::RealLoopFixture {
 public:
  PseudoDirUtilsTest() = default;
  ~PseudoDirUtilsTest() override = default;

  void test_make_file_with_contents_sizes(int content_size) {
    const std::string path = "test.config";
    std::string contentWritten(content_size, 'T');
    auto file_path = MakeFilePathWithContents(path, contentWritten);
    modular::PseudoDirServer server(std::move(file_path));
    auto fd = server.OpenAt(".");
    EXPECT_TRUE(files::IsFileAt(fd.get(), path));
    std::string contentRead;
    EXPECT_TRUE(files::ReadFileToStringAt(fd.get(), path, &contentRead));
    EXPECT_EQ(contentWritten, contentRead);
  }
};

TEST_F(PseudoDirUtilsTest, FileSmallerThanDefaultBuffer) {
  test_make_file_with_contents_sizes(kDefaultBufferSize - 10);
}

TEST_F(PseudoDirUtilsTest, FileLargerThanDefaultBuffer) {
  test_make_file_with_contents_sizes(kDefaultBufferSize + 10);
}

}  // namespace
}  // namespace modular
