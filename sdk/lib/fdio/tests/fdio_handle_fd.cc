// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zx/handle.h>
#include <lib/zx/socket.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(HandleFDTest, Close) {
  zx_handle_t h = ZX_HANDLE_INVALID;
  ASSERT_OK(zx_event_create(0u, &h));
  ASSERT_NE(h, ZX_HANDLE_INVALID);

  // fdio_handle_fd() with shared_handle = true
  int fd = fdio_handle_fd(h, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1, true);
  ASSERT_GE(fd, 0, "%s", strerror(errno));

  close(fd);

  // close(fd) has not closed the wrapped handle
  EXPECT_OK(zx_object_signal(h, 0, ZX_USER_SIGNAL_0));

  // fdio_handle_fd() with shared_handle = false
  fd = fdio_handle_fd(h, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1, false);
  ASSERT_GE(fd, 0, "%s", strerror(errno));

  close(fd);

  // close(fd) has closed the wrapped handle
  EXPECT_STATUS(zx_object_signal(h, 0, ZX_USER_SIGNAL_0), ZX_ERR_BAD_HANDLE);
}

class HandleFD : public zxtest::Test {
 protected:
  void SetUp() override {
    int fds[2];
    ASSERT_SUCCESS(pipe(fds));

    for (size_t i = 0; i < fds_.size(); ++i) {
      fds_[i].reset(fds[i]);
    }

    int flags = fcntl(fds_[0].get(), F_GETFL);
    ASSERT_GE(flags, 0, "%s", strerror(errno));
    ASSERT_SUCCESS(fcntl(fds_[0].get(), F_SETFL, flags | O_NONBLOCK));
  }

  const std::array<fbl::unique_fd, 2>& fds() { return fds_; }
  std::array<fbl::unique_fd, 2>& mutable_fds() { return fds_; }

 private:
  std::array<fbl::unique_fd, 2> fds_;
};

TEST_F(HandleFD, Pipe) {
  for (const auto& fd : fds()) {
    struct stat st;
    ASSERT_SUCCESS(fstat(fd.get(), &st));
    ASSERT_EQ(st.st_mode & S_IFMT, unsigned(S_IFIFO));
  }

  constexpr char message[] = "hello";
  ASSERT_EQ(write(fds()[1].get(), message, sizeof(message)), ssize_t(sizeof(message)), "%s",
            strerror(errno));

  int available;
  ASSERT_SUCCESS(ioctl(fds()[0].get(), FIONREAD, &available));
  EXPECT_EQ(available, ssize_t(sizeof(message)));

  char buffer[sizeof(message) + 1];
  ASSERT_EQ(read(fds()[0].get(), buffer, sizeof(buffer)), ssize_t(sizeof(message)), "%s",
            strerror(errno));
  buffer[sizeof(message)] = 0;

  EXPECT_STREQ(buffer, message);
}

TEST_F(HandleFD, TransferFD) {
  constexpr char message[] = "hello";
  ASSERT_EQ(write(fds()[1].get(), message, sizeof(message)), ssize_t(sizeof(message)), "%s",
            strerror(errno));

  // fd --> handle
  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer(mutable_fds()[0].release(), handle.reset_and_get_address()));

  // handle --> fd
  ASSERT_OK(fdio_fd_create(handle.release(), mutable_fds()[0].reset_and_get_address()));

  // Read message
  char buffer[sizeof(message) + 1];
  ASSERT_EQ(read(fds()[0].get(), buffer, sizeof(buffer)), ssize_t(sizeof(message)), "%s",
            strerror(errno));
  buffer[sizeof(message)] = 0;

  EXPECT_STREQ(buffer, message);
}

TEST(HandleFDTest, TransferDevice) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(open("/dev/zero", O_RDONLY)), "%s", strerror(errno));

  // fd --> handle
  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer(fd.release(), handle.reset_and_get_address()));

  // handle --> fd
  ASSERT_OK(fdio_fd_create(handle.release(), fd.reset_and_get_address()));

  ASSERT_SUCCESS(close(fd.release()));
}

TEST(HandleFDTest, CreateFDFromConnectedSocket) {
  zx::socket s1, s2;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &s1, &s2));

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(s1.release(), fd.reset_and_get_address()));

  constexpr char message[] = "hello";
  size_t actual;
  ASSERT_OK(s2.write(0, message, sizeof(message), &actual));
  ASSERT_EQ(actual, sizeof(message));

  char buffer[sizeof(message) + 1];
  ASSERT_EQ(read(fd.get(), buffer, sizeof(buffer)), ssize_t(sizeof(message)), "%s",
            strerror(errno));
  buffer[actual] = 0;
  EXPECT_STREQ(buffer, message);

  // Set O_NONBLOCK
  int flags = fcntl(fd.get(), F_GETFL);
  ASSERT_GE(flags, 0, "%s", strerror(errno));
  ASSERT_SUCCESS(fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK));
  ASSERT_EQ(read(fd.get(), buffer, sizeof(buffer)), -1);
  ASSERT_ERRNO(EAGAIN);
}

TEST(HandleFDTest, BindToFDInvalid) {
  {
    fdio_t* fdio = fdio_null_create();
    EXPECT_TRUE(fdio);

    // When binding and not providing a specific |fd|, the
    // |starting_fd| must be nonnegative.
    EXPECT_LT(fdio_bind_to_fd(fdio, -1, -1), 0);
    EXPECT_ERRNO(EINVAL);
  }

  {
    fdio_t* fdio = fdio_null_create();
    EXPECT_TRUE(fdio);

    // Starting with a huge |starting_fd| will fail since the table
    // does not hold so many.
    EXPECT_LT(fdio_bind_to_fd(fdio, -1, INT_MAX), 0);
    EXPECT_ERRNO(EMFILE);
  }
}
