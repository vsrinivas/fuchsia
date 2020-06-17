// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_

#include <gtest/gtest.h>

#include "fs_test.h"

namespace fs_test {

class __EXPORT BaseFileSystemTest : public testing::Test {
 public:
  BaseFileSystemTest(const TestFileSystemOptions& options)
      : fs_(TestFileSystem::Create(options).value()) {}
  ~BaseFileSystemTest();

  std::string GetPath(std::string_view relative_path) const {
    std::string path = fs_.mount_path() + "/";
    path.append(relative_path);
    return path;
  }

  TestFileSystem& fs() { return fs_; }

 protected:
  TestFileSystem fs_;
};

// Example:
//
// #include "fs_test_fixture.h"
//
// using MyTest = FileSystemTest;
//
// TEST_P(MyTest, CheckThatFooSucceeds) {
//   ...
// }
//
// INSTANTIATE_TEST_SUITE_P(FooTests, MyTest,
//                          testing::ValuesIn(AllTestFileSystems()),
//                          testing::PrintToStringParamName());

class FileSystemTest : public BaseFileSystemTest,
                       public testing::WithParamInterface<TestFileSystemOptions> {
 public:
  FileSystemTest() : BaseFileSystemTest(GetParam()) {}
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_
