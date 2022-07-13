// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <lib/zx/channel.h>

#include <src/lib/files/file.h>
#include <src/lib/files/scoped_temp_dir.h>
#include <src/lib/fxl/strings/string_printf.h>

namespace component {
namespace {

using fuchsia::sys::TerminationReason;
using sys::testing::EnclosingEnvironment;

constexpr char kRealm[] = "test";

class ComponentsBinaryTest : public gtest::TestWithEnvironmentFixture {
 protected:
  void OpenNewOutFile() {
    ASSERT_TRUE(tmp_dir_.NewTempFile(&out_file_));
    outf_ = fileno(std::fopen(out_file_.c_str(), "w"));
  }

  std::string ReadOutFile() {
    std::string out;
    if (!files::ReadFileToString(out_file_, &out)) {
      FX_LOGS(ERROR) << "Could not read output file " << out_file_;
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
    RunComponent(CreateLaunchInfo(url, args), return_code, termination_reason);
  }

  void RunComponent(fuchsia::sys::LaunchInfo launch_info, int64_t* return_code = nullptr,
                    TerminationReason* termination_reason = nullptr) {
    fuchsia::sys::ComponentControllerPtr controller;
    environment_->CreateComponent(std::move(launch_info), controller.NewRequest());

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

TEST_F(ComponentsBinaryTest, EchoStdin) {
  int64_t return_code = -1;
  TerminationReason termination_reason;
  RunComponent(ComponentsBinaryTest::UrlFromCmx("echo_stdin.cmx"), &return_code,
               &termination_reason, {std::string("hello world")});
  EXPECT_EQ(0, return_code);
  EXPECT_EQ(TerminationReason::EXITED, termination_reason);
}

TEST_F(ComponentsBinaryTest, FlatNamespaceOverridesSandbox) {
  const static std::string kTestContent = "Hello World!";

  // We're going to override /dev/class/usb-device which contains a protocol
  // for drivers, to instead be directory containing the file test.txt.
  // This will enure that the top-level directory is coming from FlatNamespace
  // instead of the global namespace.
  auto usb_device = std::make_unique<vfs::PseudoDir>();
  auto test_file = std::make_unique<vfs::PseudoFile>(
      /*max_file_size=*/1024,
      /*read_handler=*/[](std::vector<uint8_t>* output, size_t max_bytes) {
        output->resize(kTestContent.length());
        std::copy(kTestContent.cbegin(), kTestContent.cend(), output->begin());
        return ZX_OK;
      });
  usb_device->AddEntry("test.txt", std::move(test_file));
  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
  usb_device->Serve(fuchsia::io::OpenFlags::RIGHT_READABLE, std::move(server), dispatcher());

  // Map client end of connection to |usb_device| to a namespace entry.
  auto flat_namespace = fuchsia::sys::FlatNamespace::New();
  flat_namespace->paths.emplace_back("/dev/class/usb-device");
  flat_namespace->directories.emplace_back(std::move(client));

  auto launch_info = CreateLaunchInfo(UrlFromCmx("test_driver.cmx"));
  launch_info.flat_namespace = std::move(flat_namespace);

  int64_t return_code = -1;
  TerminationReason termination_reason;
  RunComponent(std::move(launch_info), &return_code, &termination_reason);

  // The test component should launch and exit with status code 0 if it's
  // able to open the file at /dev/class/usb-device/test.txt. If not, the
  // status code will be non-zero.
  EXPECT_EQ(0, return_code) << ReadOutFile();
  EXPECT_EQ(TerminationReason::EXITED, termination_reason);
}

}  // namespace
}  // namespace component
