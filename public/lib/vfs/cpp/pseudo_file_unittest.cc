// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <unistd.h>
#include <zircon/processargs.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "lib/gtest/real_loop_fixture.h"
#include "lib/vfs/cpp/pseudo_file.h"

namespace {

class FileWrapper {
 public:
  const std::string& buffer() { return buffer_; };

  vfs::BufferedPseudoFile* file() { return file_.get(); };

  static FileWrapper CreateReadWriteFile(std::string initial_str,
                                         size_t capacity) {
    return FileWrapper(true, initial_str, capacity);
  }

  static FileWrapper CreateReadOnlyFile(std::string initial_str) {
    return FileWrapper(false, initial_str, initial_str.length());
  }

 private:
  FileWrapper(bool write_allowed, std::string initial_str, size_t capacity)
      : buffer_(std::move(initial_str)) {
    auto readFn = [this](std::vector<uint8_t>* output) {
      output->resize(buffer_.length());
      std::copy(buffer_.begin(), buffer_.end(), output->begin());
      return ZX_OK;
    };

    vfs::BufferedPseudoFile::WriteHandler writeFn;
    if (write_allowed) {
      writeFn = [this](std::vector<uint8_t> input) {
        std::string str(input.size(), 0);
        std::copy(input.begin(), input.begin() + input.size(), str.begin());
        buffer_ = std::move(str);
      };
    }

    file_ = std::make_unique<vfs::BufferedPseudoFile>(
        std::move(readFn), std::move(writeFn), capacity);
  }

  std::unique_ptr<vfs::BufferedPseudoFile> file_;
  std::string buffer_;
};

class BufferedPseudoFileTest : public gtest::RealLoopFixture {
 protected:
  void AssertOpen(vfs::Node* node, async_dispatcher_t* dispatcher,
                  uint32_t flags, zx_status_t expected_status,
                  bool test_on_open_event = true) {
    fuchsia::io::NodePtr node_ptr;
    if (test_on_open_event) {
      flags |= fuchsia::io::OPEN_FLAG_DESCRIBE;
    }
    EXPECT_EQ(
        expected_status,
        node->Serve(flags, node_ptr.NewRequest().TakeChannel(), dispatcher));

    if (test_on_open_event) {
      bool on_open_called = false;
      node_ptr.events().OnOpen =
          [&](zx_status_t status, std::unique_ptr<fuchsia::io::NodeInfo> info) {
            EXPECT_FALSE(on_open_called);  // should be caleld only once
            on_open_called = true;
            EXPECT_EQ(expected_status, status);
            if (expected_status == ZX_OK) {
              ASSERT_NE(info.get(), nullptr);
              EXPECT_TRUE(info->is_file());
            } else {
              EXPECT_EQ(info.get(), nullptr);
            }
          };

      ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&]() { return on_open_called; }));
    }
  }

  int OpenAsFD(vfs::Node* node, async_dispatcher_t* dispatcher) {
    zx::channel local, remote;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
    EXPECT_EQ(ZX_OK, node->Serve(fuchsia::io::OPEN_RIGHT_READABLE |
                                     fuchsia::io::OPEN_RIGHT_WRITABLE,
                                 std::move(remote), dispatcher));
    zx_handle_t handles = local.release();
    uint32_t types = PA_FDIO_REMOTE;
    int fd = -1;
    EXPECT_EQ(ZX_OK, fdio_create_fd(&handles, &types, 1, &fd));
    return fd;
  }
};

TEST_F(BufferedPseudoFileTest, ServeOnInValidFlagsForReadWriteFile) {
  auto file_wrapper = FileWrapper::CreateReadWriteFile("test_str", 100);
  {
    SCOPED_TRACE("OPEN_FLAG_DIRECTORY");
    AssertOpen(file_wrapper.file(), dispatcher(),
               fuchsia::io::OPEN_FLAG_DIRECTORY, ZX_ERR_NOT_DIR);
  }
  uint32_t not_allowed_flags[] = {fuchsia::io::OPEN_RIGHT_ADMIN,
                                  fuchsia::io::OPEN_FLAG_CREATE,
                                  fuchsia::io::OPEN_FLAG_CREATE_IF_ABSENT,
                                  fuchsia::io::OPEN_FLAG_NO_REMOTE};
  for (auto not_allowed_flag : not_allowed_flags) {
    SCOPED_TRACE(std::to_string(not_allowed_flag));
    AssertOpen(file_wrapper.file(), dispatcher(), not_allowed_flag,
               ZX_ERR_NOT_SUPPORTED);
  }

  {
    SCOPED_TRACE("OPEN_FLAG_APPEND");
    AssertOpen(file_wrapper.file(), dispatcher(), fuchsia::io::OPEN_FLAG_APPEND,
               ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(BufferedPseudoFileTest, ServeOnInValidFlagsForReadOnlyFile) {
  auto file_wrapper = FileWrapper::CreateReadOnlyFile("test_str");
  {
    SCOPED_TRACE("OPEN_FLAG_DIRECTORY");
    AssertOpen(file_wrapper.file(), dispatcher(),
               fuchsia::io::OPEN_FLAG_DIRECTORY, ZX_ERR_NOT_DIR);
  }
  uint32_t not_allowed_flags[] = {
      fuchsia::io::OPEN_RIGHT_ADMIN,           fuchsia::io::OPEN_FLAG_CREATE,
      fuchsia::io::OPEN_FLAG_CREATE_IF_ABSENT, fuchsia::io::OPEN_FLAG_NO_REMOTE,
      fuchsia::io::OPEN_RIGHT_WRITABLE,        fuchsia::io::OPEN_FLAG_TRUNCATE};
  for (auto not_allowed_flag : not_allowed_flags) {
    SCOPED_TRACE(std::to_string(not_allowed_flag));
    AssertOpen(file_wrapper.file(), dispatcher(), not_allowed_flag,
               ZX_ERR_NOT_SUPPORTED);
  }

  {
    SCOPED_TRACE("OPEN_FLAG_APPEND");
    AssertOpen(file_wrapper.file(), dispatcher(), fuchsia::io::OPEN_FLAG_APPEND,
               ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(BufferedPseudoFileTest, ServeOnValidFlagsForReadWriteFile) {
  auto file_wrapper = FileWrapper::CreateReadWriteFile("test_str", 100);
  uint32_t allowed_flags[] = {
      fuchsia::io::OPEN_RIGHT_READABLE, fuchsia::io::OPEN_RIGHT_WRITABLE,
      fuchsia::io::OPEN_FLAG_NODE_REFERENCE, fuchsia::io::OPEN_FLAG_TRUNCATE};
  for (auto allowed_flag : allowed_flags) {
    SCOPED_TRACE(std::to_string(allowed_flag));
    AssertOpen(file_wrapper.file(), dispatcher(), allowed_flag, ZX_OK);
  }
}

TEST_F(BufferedPseudoFileTest, ServeOnValidFlagsForReadOnlyFile) {
  auto file_wrapper = FileWrapper::CreateReadOnlyFile("test_str");
  uint32_t allowed_flags[] = {fuchsia::io::OPEN_RIGHT_READABLE,
                              fuchsia::io::OPEN_FLAG_NODE_REFERENCE};
  for (auto allowed_flag : allowed_flags) {
    SCOPED_TRACE(std::to_string(allowed_flag));
    AssertOpen(file_wrapper.file(), dispatcher(), allowed_flag, ZX_OK);
  }
}

TEST_F(BufferedPseudoFileTest, Simple) {
  auto file_wrapper = FileWrapper::CreateReadWriteFile("test_str", 100);
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  loop.StartThread("vfs test thread");

  int fd = OpenAsFD(file_wrapper.file(), loop.dispatcher());
  ASSERT_LE(0, fd);

  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  ASSERT_EQ(5, pread(fd, buffer, 5, 0));
  EXPECT_STREQ("test_", buffer);

  ASSERT_EQ(4, write(fd, "abcd", 4));
  ASSERT_EQ(5, pread(fd, buffer, 5, 0));
  EXPECT_STREQ("abcd_", buffer);

  ASSERT_GE(0, close(fd));
  loop.RunUntilIdle();

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&file_wrapper]() { return file_wrapper.buffer() == "abcd_str"; },
      zx::sec(1)))
      << file_wrapper.buffer();
}

}  // namespace
