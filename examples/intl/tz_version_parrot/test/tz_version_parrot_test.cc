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
#include <zircon/errors.h>

#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace {

using fuchsia::sys::ComponentControllerPtr;
using fuchsia::sys::LaunchInfo;
using sys::testing::TerminationResult;
using sys::testing::TestWithEnvironment;

constexpr char kParrotWithoutTzDataPackage[] =
    "fuchsia-pkg://fuchsia.com/tz_version_parrot#meta/tz_version_parrot.cmx";

constexpr char kParrotWithTzDataPackage[] =
    "fuchsia-pkg://fuchsia.com/tz_version_parrot#meta/tz_version_parrot_with_tzdata.cmx";

constexpr char kParrotWithTzDataPackageWrongRevision[] =
    "fuchsia-pkg://fuchsia.com/tz_version_parrot#meta/"
    "tz_version_parrot_with_tzdata_wrong_revision.cmx";

class TzVersionParrotTest : public TestWithEnvironment {
 protected:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    OpenNewOutFile();
  }

  void TearDown() override {
    CloseOutFile();
    TestWithEnvironment::TearDown();
  }

  void OpenNewOutFile() {
    ASSERT_TRUE(temp_dir_.NewTempFile(&out_file_path_));
    out_file_ = std::fopen(out_file_path_.c_str(), "w");
  }

  void CloseOutFile() {
    if (out_file_ != nullptr) {
      std::fclose(out_file_);
    }
  }

  // Read the contents of the file at |path| into |contents|.
  void ReadFile(const std::string& path, std::string& contents) {
    ASSERT_TRUE(files::ReadFileToString(path, &contents)) << "Could not read file " << path;
  }

  // Read the contents of the file at |out_file_path_| into |contents|.
  void ReadStdOutFile(std::string& contents) { ReadFile(out_file_path_, contents); }

  ComponentControllerPtr LaunchParrot(const char cmx[]) {
    LaunchInfo launch_info{
        .url = cmx,
        .out = sys::CloneFileDescriptor(fileno(out_file_)),
        .err = sys::CloneFileDescriptor(STDERR_FILENO),

    };
    ComponentControllerPtr controller;
    CreateComponentInCurrentEnvironment(std::move(launch_info), controller.NewRequest());
    return controller;
  }

 private:
  files::ScopedTempDir temp_dir_;
  std::string out_file_path_;
  FILE* out_file_ = nullptr;
};

TEST_F(TzVersionParrotTest, NoTzResFiles) {
  ComponentControllerPtr controller = LaunchParrot(kParrotWithoutTzDataPackage);

  TerminationResult result{};
  ASSERT_TRUE(RunComponentUntilTerminated(std::move(controller), &result));
  ASSERT_EQ(0, result.return_code);

  std::string actual_output;
  ReadStdOutFile(actual_output);

  std::regex tz_version_regex(R"(20[0-9][0-9][a-z])");
  ASSERT_TRUE(std::regex_search(actual_output, tz_version_regex));
}

TEST_F(TzVersionParrotTest, WithTzResFiles) {
  ComponentControllerPtr controller = LaunchParrot(kParrotWithTzDataPackage);

  TerminationResult result{};
  ASSERT_TRUE(RunComponentUntilTerminated(std::move(controller), &result));
  ASSERT_EQ(0, result.return_code);

  std::string actual_output;
  ReadStdOutFile(actual_output);

  std::regex tz_version_regex(R"(20[0-9][0-9][a-z])");
  ASSERT_TRUE(std::regex_search(actual_output, tz_version_regex));
}

TEST_F(TzVersionParrotTest, WithTzResFilesWrongRevision) {
  ComponentControllerPtr controller = LaunchParrot(kParrotWithTzDataPackageWrongRevision);

  TerminationResult result{};
  ASSERT_TRUE(RunComponentUntilTerminated(std::move(controller), &result));
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, result.return_code);
}

}  // namespace
