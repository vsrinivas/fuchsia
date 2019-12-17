// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <fstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace tests {

using fuchsia::sys::ComponentControllerPtr;
using fuchsia::sys::LaunchInfo;
using fuchsia::sys::TerminationReason;
using sys::testing::TerminationResult;
using sys::testing::TestWithEnvironment;

constexpr char kManifest[] = "fuchsia-pkg://fuchsia.com/tz-util#meta/tz-util.cmx";

// A quick test fixture that stores both the file path and handle.
struct FileData {
  std::string path;
  FILE* handle;
  FileData(const std::string& path) : path(path), handle(std::fopen(path.c_str(), "w")) {}
  ~FileData() {
    if (handle != nullptr) {
      std::fclose(handle);
    }
  }
  std::string ReadOrDie() const {
    EXPECT_TRUE(handle != nullptr) << "null file handle: " << path;
    fsync(fileno(handle));
    std::string contents;
    EXPECT_TRUE(files::ReadFileToString(path, &contents)) << "could not read: " << path;
    return contents;
  }
};

class TzUtilTest : public TestWithEnvironment {
 protected:
  void SetUp() override { TestWithEnvironment::SetUp(); }

  // Returns a different tmpname every time.
  // Requires sandbox.features = [ "isolated-temp" ].
  std::string TmpName() {
    std::string tempname;
    temp_dir_.NewTempFile(&tempname);
    return tempname;
  }

  ComponentControllerPtr Launch(FileData* out, const std::initializer_list<std::string>& args) {
    LaunchInfo launch_info{
        .url = kManifest,
        .arguments = std::vector<std::string>({args}),
        .err = sys::CloneFileDescriptor(fileno(out->handle)),
    };

    ComponentControllerPtr controller;
    CreateComponentInCurrentEnvironment(std::move(launch_info), controller.NewRequest());
    return controller;
  }

  files::ScopedTempDir temp_dir_;
};

TEST_F(TzUtilTest, Set) {
  FileData set_file(TmpName());
  TerminationResult result;
  ASSERT_TRUE(RunComponentUntilTerminated(Launch(&set_file, {"--set_timezone_id=Europe/Amsterdam"}),
                                          &result));
  EXPECT_EQ(TerminationReason::EXITED, result.reason);
  EXPECT_EQ(0, result.return_code);
  ASSERT_EQ("", set_file.ReadOrDie());

  FileData get_file(TmpName());
  ASSERT_TRUE(RunComponentUntilTerminated(Launch(&get_file, {"--get_timezone_id"}), &result));
  EXPECT_EQ(TerminationReason::EXITED, result.reason);
  EXPECT_EQ(0, result.return_code);
  ASSERT_EQ("Europe/Amsterdam\n", get_file.ReadOrDie());
}

TEST_F(TzUtilTest, GetTimezoneOffsetMinutes) {
  FileData set_file(TmpName());
  TerminationResult result;
  ASSERT_TRUE(RunComponentUntilTerminated(Launch(&set_file, {"--set_timezone_id=CST"}), &result));
  EXPECT_EQ(TerminationReason::EXITED, result.reason);
  EXPECT_EQ(0, result.return_code);
  ASSERT_EQ("", set_file.ReadOrDie());

  FileData get_file(TmpName());
  ASSERT_TRUE(RunComponentUntilTerminated(Launch(&get_file, {"--get_offset_minutes"}), &result));
  EXPECT_EQ(TerminationReason::EXITED, result.reason);
  EXPECT_EQ(0, result.return_code);
  ASSERT_EQ("-360\n", get_file.ReadOrDie());
}

TEST_F(TzUtilTest, SetInvalidTimezoneRejected) {
  FileData get_file(TmpName());
  TerminationResult result;
  ASSERT_TRUE(
      RunComponentUntilTerminated(Launch(&get_file, {"--set_timezone_id=Roger/Rabbit"}), &result));
  EXPECT_EQ(TerminationReason::EXITED, result.reason);
  EXPECT_EQ(1, result.return_code);

  ASSERT_EQ("ERROR: Unable to set ID: 1\n", get_file.ReadOrDie());
}

}  // namespace tests
