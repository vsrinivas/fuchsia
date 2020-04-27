// Copyright 2018 The Fuchsia Authors. All rights reserved.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace intl_wisdom {

using fuchsia::sys::ComponentControllerPtr;
using fuchsia::sys::LaunchInfo;
using sys::testing::TestWithEnvironment;

// The C++ version of the wisdom client-server example.
constexpr char kIntlWisdomClientPackage[] =
    "fuchsia-pkg://fuchsia.com/intl_wisdom#meta/intl_wisdom_client.cmx";

// The Rust version of the wisdom client-server example.
constexpr char kIntlWisdomClientRustPackage[] =
    "fuchsia-pkg://fuchsia.com/intl_wisdom_rust#meta/intl_wisdom_client_rust.cmx";

// Integration test for IntlWisdomClient and IntlWisdomServer.
//
// Starts a client, which starts a server and asks it for wisdom. Compares the
// entire STDOUT output of the client (including the server's response) to an
// expected output file.
class IntlWisdomTest : public TestWithEnvironment {
 protected:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    OpenNewOutFiles();
  }

  void TearDown() override {
    CloseOutFiles();
    TestWithEnvironment::TearDown();
  }

  void OpenNewOutFiles() {
    ASSERT_TRUE(temp_dir_.NewTempFile(&out_file_path_));
    out_file_ = std::fopen(out_file_path_.c_str(), "w");
    ASSERT_TRUE(temp_dir_.NewTempFile(&err_file_path_));
    err_file_ = std::fopen(err_file_path_.c_str(), "w");
  }

  void CloseOutFiles() {
    if (out_file_ != nullptr) {
      std::fclose(out_file_);
    }
    if (err_file_ != nullptr) {
      std::fclose(err_file_);
    }
  }

  // Read the contents of the file at |path| into |contents|.
  static void ReadFile(const std::string& path, std::string& contents) {
    ASSERT_TRUE(files::ReadFileToString(path, &contents)) << "Could not read file " << path;
  }

  ComponentControllerPtr LaunchClientWithServer(const std::string& url) {
    LaunchInfo launch_info{
        .url = url,
        .out = sys::CloneFileDescriptor(fileno(out_file_)),
        .err = sys::CloneFileDescriptor(fileno(err_file_)),
        .arguments = std::vector<std::string>({
            "--timestamp=2018-11-01T12:34:56Z",
            "--timezone=America/Los_Angeles",
        }),
    };

    ComponentControllerPtr controller;
    CreateComponentInCurrentEnvironment(std::move(launch_info), controller.NewRequest());
    return controller;
  }

  const std::string& out_file_path() const { return out_file_path_; }

  const std::string& err_file_path() const { return err_file_path_; }

  // Syncs the files used for recording stdout and stderr.
  void SyncWrites() {
    fsync(fileno(out_file_));
    fsync(fileno(err_file_));
  }

  void RunWisdomClientAndServer(const std::string& package_url) {
    std::string expected_output;
    IntlWisdomTest::ReadFile("/pkg/data/golden-output.txt", expected_output);

    ComponentControllerPtr controller = LaunchClientWithServer(package_url);
    ASSERT_TRUE(RunComponentUntilTerminated(std::move(controller), nullptr));
    // Ensures that the data we just wrote is available for subsequent reading
    // in the assertions.  Not doing so can result in assertions not seeing
    // the just-written content.
    SyncWrites();

    std::string actual_output;
    ReadFile(out_file_path(), actual_output);
    std::string stderr_output;
    ReadFile(err_file_path(), stderr_output);
    ASSERT_EQ(actual_output, expected_output) << "stdout:\n"
                                              << actual_output << "\n:stderr:\n"
                                              << stderr_output;
  }

 private:
  files::ScopedTempDir temp_dir_;
  std::string out_file_path_;
  std::string err_file_path_;
  FILE* out_file_ = nullptr;
  FILE* err_file_ = nullptr;
};

TEST_F(IntlWisdomTest, RunWisdomClientAndServerCPP) {
  RunWisdomClientAndServer(kIntlWisdomClientPackage);
}

TEST_F(IntlWisdomTest, RunWisdomClientAndServerRust) {
  RunWisdomClientAndServer(kIntlWisdomClientRustPackage);
}

}  // namespace intl_wisdom
