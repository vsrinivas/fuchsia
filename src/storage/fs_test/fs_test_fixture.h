// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_

#include <gtest/gtest.h>

#include "fs_test.h"

namespace fs_test {

// Example:
//
// #include "fs_test_fixture.h"
//
// TEST_P(FileSystemTest, CheckThatFooSucceeds) {
//   ...
// }
//
// INSTANTIATE_TEST_SUITE_P(FooTests, FileSystemTest,
//                          testing::ValuesIn(AllTestFileSystems()),
//                          testing::PrintToStringParamName());

class FileSystemTest : public testing::Test,
                       public testing::WithParamInterface<TestFileSystemOptions> {
 public:
  FileSystemTest() : fs_(TestFileSystem::Create(GetParam()).value()) {}

  std::string GetPath(std::string_view relative_path) const {
    std::string path = fs_.mount_path() + "/";
    path.append(relative_path);
    return path;
  }

 protected:
  TestFileSystem fs_;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_
