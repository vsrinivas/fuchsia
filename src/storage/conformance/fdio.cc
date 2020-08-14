// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/44924): We should come up with a better way of testing client libraries,
// rather than testing every client against every server. See issue for details.

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/service/cpp/service.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <iostream>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"

fidl::InterfaceHandle<fuchsia::io::Directory> StartTestHarness(
    sys::ComponentContext* context, std::string harness_name,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  fidl::InterfaceHandle<fuchsia::io::Directory> svc;
  fuchsia::sys::LaunchInfo info{
      .url = "fuchsia-pkg://fuchsia.com/" + harness_name + "#meta/" + harness_name + ".cmx",
      .directory_request = svc.NewRequest().TakeChannel(),
  };

  auto launcher = context->svc()->Connect<fuchsia::sys::Launcher>();
  launcher->CreateComponent(std::move(info), std::move(controller));
  return svc;
}

class FdioTest : public testing::Test {
 public:
  // This will be initialized by |main|.
  static sys::ComponentContext* component_context;

  // This will be set by |main| multiple times.
  static const char* harness_name;

  static void SetUpTestSuite() {
    auto svc = StartTestHarness(component_context, harness_name, controller.NewRequest());
    auto default_service = sys::OpenServiceAt<fuchsia::io::test::Harness>(svc);
    // TODO(fxbug.dev/33880): Add io2 tests when ready.
    v1_test_cases = default_service.v1().Connect().Bind();
  }

  static void TearDownTestSuite() {
    // This will also terminate the test harness component.
    auto _ = controller.Unbind();
  }

 protected:
  FdioTest() {
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &client_end_, &server_end_));
    EXPECT_EQ(ZX_OK, fdio_ns_get_installed(&ns_));
    if (!ns_) {
      return;
    }
    EXPECT_EQ(ZX_OK, fdio_ns_bind(ns_, kTestPath, client_end_.release()));
  }

  ~FdioTest() override { EXPECT_EQ(ZX_OK, fdio_ns_unbind(ns_, kTestPath)); }

  static void FillBuffer(std::vector<char>* buf) {
    buf->resize(kTestVmoSize);
    for (size_t i = 0; i < kTestVmoSize; i++) {
      // Mod 256 by truncating.
      (*buf)[i] = static_cast<char>(i);
    }
  }

  static zx::vmo MakeTestVmo(const std::vector<char>& buffer) {
    zx::vmo ret;
    EXPECT_EQ(ZX_OK, zx::vmo::create(kTestVmoSize, 0, &ret));
    EXPECT_EQ(ZX_OK, ret.write(&buffer[0], 0, kTestVmoSize));
    return ret;
  }

  zx::channel&& TakeServerEnd() { return std::move(server_end_); }

  // To test fdio, the root server directory will be bound to this path
  // in the namespace.
  constexpr static const char* kTestPath = "/fdio_test";

  constexpr static const char* kVmoFileName = "vmo_file";

  constexpr static size_t kTestVmoSize = 4096;

  static fuchsia::sys::ComponentControllerPtr controller;

  static fidl::InterfacePtr<fuchsia::io::test::TestCases> v1_test_cases;

 private:
  zx::channel client_end_;
  zx::channel server_end_;
  fdio_ns_t* ns_;
  std::vector<char> golden_buffer_;
};

sys::ComponentContext* FdioTest::component_context = {};
const char* FdioTest::harness_name = nullptr;
fuchsia::sys::ComponentControllerPtr FdioTest::controller = {};
fidl::InterfacePtr<fuchsia::io::test::TestCases> FdioTest::v1_test_cases = {};

TEST_F(FdioTest, OpenEmptyDirectory) {
  v1_test_cases->GetEmptyDirectory(TakeServerEnd());
  fbl::unique_fd dir_fd(open(kTestPath, O_RDONLY));
  ASSERT_TRUE(dir_fd.is_valid()) << "errno is " << errno;

  struct stat statbuf;
  EXPECT_EQ(0, fstat(dir_fd.get(), &statbuf)) << "errno is" << errno;
  EXPECT_EQ(1ul, statbuf.st_nlink);
  EXPECT_EQ(0, statbuf.st_size);
}

TEST_F(FdioTest, ReadFromVmoFile) {
  std::vector<char> golden_buffer;
  FillBuffer(&golden_buffer);
  auto buffer = fuchsia::mem::Range{
      .vmo = MakeTestVmo(golden_buffer), .offset = 0, .size = golden_buffer.size()};
  v1_test_cases->GetDirectoryWithVmoFile(std::move(buffer), TakeServerEnd());

  std::string vmo_path = std::string(kTestPath) + "/" + kVmoFileName;
  fbl::unique_fd vmo_fd(open(vmo_path.c_str(), O_RDONLY));
  ASSERT_TRUE(vmo_fd.is_valid()) << "errno is " << errno;

  // Reading works.
  std::vector<char> read_buffer(kTestVmoSize);
  EXPECT_EQ(static_cast<ssize_t>(kTestVmoSize),
            read(vmo_fd.get(), read_buffer.data(), kTestVmoSize));
  EXPECT_EQ(golden_buffer, read_buffer);
}

TEST_F(FdioTest, GetAttrVmoFile) {
  std::vector<char> golden_buffer;
  FillBuffer(&golden_buffer);
  auto buffer = fuchsia::mem::Range{
      .vmo = MakeTestVmo(golden_buffer), .offset = 0, .size = golden_buffer.size()};
  v1_test_cases->GetDirectoryWithVmoFile(std::move(buffer), TakeServerEnd());

  std::string vmo_path = std::string(kTestPath) + "/" + kVmoFileName;
  fbl::unique_fd vmo_fd(open(vmo_path.c_str(), O_RDONLY));
  ASSERT_TRUE(vmo_fd.is_valid()) << "errno is " << errno;

  struct stat statbuf;
  EXPECT_EQ(0, fstat(vmo_fd.get(), &statbuf)) << "errno is" << errno;
  EXPECT_EQ(kTestVmoSize, static_cast<size_t>(statbuf.st_size));
}

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  FdioTest::component_context = context.get();

  testing::InitGoogleTest(&argc, argv);

  int exit_code = 0;
  for (const auto& harness_name : {
           "io_conformance_harness_sdkcpp",
           "io_conformance_harness_ulibfs",
           "io_conformance_harness_rust_pseudo_fs_mt",
       }) {
    FdioTest::harness_name = harness_name;
    std::cout << "----" << std::endl;
    std::cout << "---- Selecting testing harness: " << harness_name << std::endl;
    std::cout << "----" << std::endl;
    int result = RUN_ALL_TESTS();
    if (result != 0) {
      exit_code = result;
    }
  }
  return exit_code;
}
