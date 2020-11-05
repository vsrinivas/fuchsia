// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/procfs/cpp/process.h>
#include <lib/zx/channel.h>

#include <memory>
#include <utility>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"

namespace {

class ProcessHost {
 public:
  ProcessHost()
      : loop_(&kAsyncLoopConfigNeverAttachToThread),
        process_dir_(procfs::CreateProcessDir(loop_.dispatcher())) {
    loop_.StartThread("procfs test thread");
  }

  ~ProcessHost() {
    loop_.Shutdown();
  }

  // The returned file descriptor must be closed before the |ProcessHost| is destructed.
  fbl::unique_fd Open(uint32_t flags = fuchsia::io::OPEN_RIGHT_READABLE) {
    zx::channel client_end, server_end;
    zx_status_t status = zx::channel::create(0, &client_end, &server_end);
    if (status != ZX_OK) {
      return fbl::unique_fd();
    }
    status = process_dir_->Serve(flags, std::move(server_end), loop_.dispatcher());
    if (status != ZX_OK) {
      return fbl::unique_fd();
    }

    fbl::unique_fd fd;
    status = fdio_fd_create(client_end.release(), fd.reset_and_get_address());
    if (status != ZX_OK) {
      return fbl::unique_fd();
    }
    return fd;
  }

 private:
  async::Loop loop_;
  std::unique_ptr<vfs::PseudoDir> process_dir_;
};

TEST(ProcessDir, ReadEnviron) {
  ProcessHost host;
  auto fd = host.Open();
  ASSERT_TRUE(fd.is_valid());

  std::string content;
  ASSERT_TRUE(files::ReadFileToStringAt(fd.get(), "environ", &content));
  EXPECT_EQ(std::string::npos, content.find("THE_TEXT_WE_EXPECT"));

  setenv("PROCFS_TEST_VAR", "THE_TEXT_WE_EXPECT", 1);

  ASSERT_TRUE(files::ReadFileToStringAt(fd.get(), "environ", &content));
  EXPECT_NE(std::string::npos, content.find("THE_TEXT_WE_EXPECT"));

  unsetenv("PROCFS_TEST_VAR");

  ASSERT_TRUE(files::ReadFileToStringAt(fd.get(), "environ", &content));
  EXPECT_EQ(std::string::npos, content.find("THE_TEXT_WE_EXPECT"));
}

TEST(ProcessDir, CurrentWorkingDirectory) {
  ProcessHost host;
  auto fd = host.Open();
  ASSERT_TRUE(fd.is_valid());

  char buffer[PATH_MAX];
  getcwd(buffer, sizeof(buffer));

  ASSERT_EQ(0, chdir("/pkg/test"));

  fbl::unique_fd cwd(openat(fd.get(), "cwd", O_DIRECTORY | O_RDONLY));
  ASSERT_TRUE(cwd.is_valid());

  fbl::unique_fd probe(openat(cwd.get(), "procfs_cpp_unittests", O_RDONLY));
  ASSERT_TRUE(probe.is_valid());

  ASSERT_EQ(0, chdir("/pkg/lib"));

  probe.reset(openat(cwd.get(), "procfs_cpp_unittests", O_DIRECTORY | O_RDONLY));
  ASSERT_FALSE(probe.is_valid());

  ASSERT_EQ(0, chdir(buffer));
}

}  // namespace
