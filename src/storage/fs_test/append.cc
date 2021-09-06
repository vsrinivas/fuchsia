// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <thread>
#include <tuple>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using AppendTest = FilesystemTest;

TEST_P(AppendTest, Append) {
  char buf[4096];
  const char* hello = "Hello, ";
  const char* world = "World!\n";
  struct stat st;

  const std::string alpha = GetPath("alpha");
  fbl::unique_fd fd(open(alpha.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);

  // Write "hello"
  ASSERT_EQ(strlen(hello), strlen(world));
  ASSERT_EQ(write(fd.get(), hello, strlen(hello)), static_cast<ssize_t>(strlen(hello)));
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd.get(), buf, strlen(hello)), static_cast<ssize_t>(strlen(hello)));
  ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);

  // At the start of the file, write "world"
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), world, strlen(world)), static_cast<ssize_t>(strlen(world)));
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd.get(), buf, strlen(world)), static_cast<ssize_t>(strlen(world)));

  // Ensure that the file contains "world", but not "hello"
  ASSERT_EQ(strncmp(buf, world, strlen(world)), 0);
  ASSERT_EQ(stat(alpha.c_str(), &st), 0);
  ASSERT_EQ(st.st_size, (off_t)strlen(world));
  ASSERT_EQ(unlink(alpha.c_str()), 0);
  ASSERT_EQ(close(fd.release()), 0);

  fd.reset(open(alpha.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644));
  ASSERT_TRUE(fd);

  // Write "hello"
  ASSERT_EQ(strlen(hello), strlen(world));
  ASSERT_EQ(write(fd.get(), hello, strlen(hello)), static_cast<ssize_t>(strlen(hello)));
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd.get(), buf, strlen(hello)), static_cast<ssize_t>(strlen(hello)));
  ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);

  // At the start of the file, write "world"
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), world, strlen(world)), static_cast<ssize_t>(strlen(hello)));
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd.get(), buf, strlen(hello) + strlen(world)),
            static_cast<ssize_t>(strlen(hello) + strlen(world)));

  // Ensure that the file contains both "hello" and "world"
  ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);
  ASSERT_EQ(strncmp(buf + strlen(hello), world, strlen(world)), 0);
  ASSERT_EQ(stat(alpha.c_str(), &st), 0);
  ASSERT_EQ(st.st_size, (off_t)(strlen(hello) + strlen(world)));
  ASSERT_EQ(unlink(alpha.c_str()), 0);
  ASSERT_EQ(close(fd.release()), 0);
}

TEST_P(AppendTest, AppendOnClone) {
  enum AppendState {
    Append,
    NoAppend,
  };

  auto verify_append = [](fbl::unique_fd& fd, AppendState appendState) {
    // Ensure we have a file of non-zero size.
    char buf[32];
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
    struct stat st;
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    off_t size = st.st_size;

    // Write at the 'start' of the file.
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
    ASSERT_EQ(fstat(fd.get(), &st), 0);

    switch (appendState) {
      case Append:
        // Even though we wrote to the 'start' of the file, it
        // appends to the end if the file was opened as O_APPEND.
        ASSERT_EQ(st.st_size, size + static_cast<off_t>(sizeof(buf)));
        ASSERT_EQ(fcntl(fd.get(), F_GETFL), O_APPEND | O_RDWR);
        break;
      case NoAppend:
        // We wrote to the start of the file, so the size
        // should be unchanged.
        ASSERT_EQ(st.st_size, size);
        ASSERT_EQ(fcntl(fd.get(), F_GETFL), O_RDWR);
        break;
      default:
        ASSERT_TRUE(false);
    }
  };

  const std::string append_clone = GetPath("append_clone");
  fbl::unique_fd fd(open(append_clone.c_str(), O_RDWR | O_CREAT | O_APPEND));
  ASSERT_TRUE(fd) << strerror(errno);
  // Verify the file was originally opened as append.
  ASSERT_NO_FATAL_FAILURE(verify_append(fd, Append));

  // Verify we can toggle append off and back on.
  ASSERT_EQ(fcntl(fd.get(), F_SETFL, 0), 0);
  ASSERT_NO_FATAL_FAILURE(verify_append(fd, NoAppend));
  ASSERT_EQ(fcntl(fd.get(), F_SETFL, O_APPEND), 0);
  ASSERT_NO_FATAL_FAILURE(verify_append(fd, Append));

  // Verify that cloning the fd doesn't lose the APPEND flag.
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(fdio_fd_clone(fd.get(), &handle), ZX_OK);

  int raw_fd = -1;
  ASSERT_EQ(fdio_fd_create(handle, &raw_fd), ZX_OK);
  fbl::unique_fd cloned_fd(raw_fd);
  ASSERT_NO_FATAL_FAILURE(verify_append(cloned_fd, Append));

  ASSERT_EQ(unlink(append_clone.c_str()), 0);
}

using ParamType = std::tuple<TestFilesystemOptions, /*thread_count=*/int>;

class AppendAtomicTest : public BaseFilesystemTest, public testing::WithParamInterface<ParamType> {
 public:
  AppendAtomicTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  int thread_count() const { return std::get<1>(GetParam()); }
};

TEST_P(AppendAtomicTest, MultiThreadedTest) {
  constexpr int kWriteLength = 32;
  constexpr int kNumWrites = 128;
  const std::string append_atomic = GetPath("append-atomic");

  // Create a group of threads which all append 'i' to a file.
  // At the end of this test, we should see:
  // - A file of length kWriteLength * kNumWrites * thread_count().
  // - kWriteLength * kNumWrites of the character 'i' for all
  // values of i in the range [0, thread_count()).
  // - Those 'i's should be grouped in units of kWriteLength.
  std::thread threads[thread_count()];
  for (int i = 0; i < thread_count(); i++) {
    threads[i] = std::thread([&append_atomic, i]() {
      fbl::unique_fd fd(open(append_atomic.c_str(), O_WRONLY | O_CREAT | O_APPEND));
      if (!fd) {
        return -1;
      }

      char buf[kWriteLength];
      memset(buf, static_cast<int>(i + 1), sizeof(buf));

      for (size_t j = 0; j < kNumWrites; j++) {
        if (write(fd.get(), buf, sizeof(buf)) != sizeof(buf)) {
          return -1;
        }
      }

      return close(fd.release());
    });
  }

  for (std::thread& thread : threads) {
    thread.join();
  }

  // Verify the contents of the file
  fbl::unique_fd fd(open(append_atomic.c_str(), O_RDONLY));
  ASSERT_GE(fd.get(), 0) << "Can't reopen file for verification";
  struct stat st;
  ASSERT_EQ(fstat(fd.get(), &st), 0);
  ASSERT_EQ(st.st_size, static_cast<off_t>(kWriteLength * kNumWrites * thread_count()));

  char buf[kWriteLength * kNumWrites * thread_count()];
  ASSERT_EQ(read(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));

  std::vector<int> counts(thread_count() + 1);
  for (size_t i = 0; i < sizeof(buf); i += kWriteLength) {
    size_t val = static_cast<size_t>(buf[i]);
    ASSERT_LE(val, counts.size()) << "Read unexpected value from file";
    counts[val]++;
    char tmp[kWriteLength];
    memset(tmp, buf[i], sizeof(tmp));

    ASSERT_EQ(memcmp(&buf[i], tmp, sizeof(tmp)), 0) << "Non-atomic Append Detected";
  }

  for (size_t i = 1; i < counts.size(); i++) {
    ASSERT_EQ(counts[i], kNumWrites) << "Unexpected number of writes from thread " << i;
  }

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(append_atomic.c_str()), 0);
}

std::string GetParamDescription(const testing::TestParamInfo<ParamType>& param) {
  std::stringstream s;
  s << std::get<0>(param.param) << "WithThreadCount" << std::get<1>(param.param);
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, AppendTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, AppendAtomicTest,
                         testing::Combine(testing::ValuesIn(AllTestFilesystems()),
                                          testing::Values(1, 2, 5, 10)),
                         GetParamDescription);

}  // namespace
}  // namespace fs_test
