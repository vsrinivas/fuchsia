// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace component {
namespace {

using fuchsia::sys::TerminationReason;
using sys::testing::EnclosingEnvironment;

constexpr char kRealm[] = "test";

class ComponentsBinaryTest : public sys::testing::TestWithEnvironment {
 protected:
  void OpenNewOutFile() {
    ASSERT_TRUE(tmp_dir_.NewTempFile(&out_file_));
    outf_ = fileno(std::fopen(out_file_.c_str(), "w"));
  }

  std::string ReadOutFile() {
    std::string out;
    if (!files::ReadFileToString(out_file_, &out)) {
      FXL_LOG(ERROR) << "Could not read output file " << out_file_;
      return "";
    }
    return out;
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url,
                                            const std::vector<std::string>& args = {}) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.arguments = args;
    launch_info.out = sys::CloneFileDescriptor(outf_);
    launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
    return launch_info;
  }

  static std::string UrlFromCmx(const std::string& cmx) {
    return fxl::StringPrintf("fuchsia-pkg://fuchsia.com/components_binary_tests#meta/%s",
                             cmx.c_str());
  }

  void RunComponent(const std::string& url, int64_t* return_code = nullptr,
                    TerminationReason* termination_reason = nullptr,
                    const std::vector<std::string>& args = {}) {
    fuchsia::sys::ComponentControllerPtr controller;
    environment_->CreateComponent(CreateLaunchInfo(url, std::move(args)), controller.NewRequest());

    bool terminated = false;
    controller.events().OnTerminated = [&terminated, &return_code, &termination_reason](
                                           int64_t code, TerminationReason reason) {
      if (return_code != nullptr) {
        *return_code = code;
      }
      if (termination_reason != nullptr) {
        *termination_reason = reason;
      }
      terminated = true;
    };
    RunLoopUntil([&terminated] { return terminated; });
  }

  ComponentsBinaryTest() {
    OpenNewOutFile();

    environment_ = CreateNewEnclosingEnvironment(kRealm, CreateServices());
  }

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  files::ScopedTempDir tmp_dir_;
  std::string out_file_;
  int outf_;
};

// We therefore test that targeting a binary by a component manifest works, that
// argv0 properly propagates the binary path, and that the args field in the
// manifest is being properly passed through to the component.
TEST_F(ComponentsBinaryTest, EchoNoArgs) {
  int64_t return_code = -1;
  RunComponent(ComponentsBinaryTest::UrlFromCmx("echo1.cmx"), &return_code);
  EXPECT_EQ(0, return_code);
  std::string output = ReadOutFile();
  ASSERT_EQ(output, "/pkg/bin/echo1\n");
}

TEST_F(ComponentsBinaryTest, EchoHelloWorld) {
  int64_t return_code = -1;
  RunComponent(ComponentsBinaryTest::UrlFromCmx("echo2.cmx"), &return_code);
  EXPECT_EQ(0, return_code);
  std::string output = ReadOutFile();
  ASSERT_EQ(output, "/pkg/bin/echo2 helloworld\n");
}

TEST_F(ComponentsBinaryTest, GetEnvMatched) {
  int64_t return_code = -1;
  RunComponent(ComponentsBinaryTest::UrlFromCmx("getenv1.cmx"), &return_code);
  EXPECT_EQ(0, return_code);
  std::string output = ReadOutFile();
  ASSERT_EQ(output, "FOO=bar BAR=baz\n");
}

TEST_F(ComponentsBinaryTest, GetEnvMismatch) {
  int64_t return_code = -1;
  RunComponent(ComponentsBinaryTest::UrlFromCmx("getenv2.cmx"), &return_code);
  EXPECT_EQ(0, return_code);
  std::string output = ReadOutFile();
  ASSERT_EQ(output, "FOO=bar BAR=NULL\n");
}

TEST_F(ComponentsBinaryTest, UnallowedDeprecatedShellFailsToLaunch) {
  int64_t return_code = -1;
  TerminationReason termination_reason;
  RunComponent(ComponentsBinaryTest::UrlFromCmx("echo_deprecated_shell.cmx"), &return_code,
               &termination_reason);
  EXPECT_NE(TerminationReason::EXITED, termination_reason);
}

}  // namespace
}  // namespace component
