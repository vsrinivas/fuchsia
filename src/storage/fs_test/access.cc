// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/cpp/caller.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>
#include <unittest/unittest.h>

#include "filesystems.h"
#include "misc.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

bool TestAccessReadable() {
  BEGIN_TEST;

  const char* filename = "::alpha";

  // Try writing a string to a file
  fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  const char buf[] = "Hello, World!\n";
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));
  ASSERT_EQ(close(fd.release()), 0);

  // Re-open as readonly
  fd.reset(open(filename, O_RDONLY, 0644));

  // Reading is allowed
  char tmp[sizeof(buf)];
  ASSERT_EQ(read(fd.get(), tmp, sizeof(tmp)), sizeof(tmp));
  ASSERT_EQ(memcmp(buf, tmp, sizeof(tmp)), 0);

  // Writing is disallowed
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), -1);
  ASSERT_EQ(errno, EBADF);
  errno = 0;

  // Truncating is disallowed
  ASSERT_EQ(ftruncate(fd.get(), 0), -1);
  ASSERT_EQ(errno, EBADF);
  errno = 0;

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename), 0);

  END_TEST;
}

bool TestAccessWritable() {
  BEGIN_TEST;

  const char* filename = "::alpha";

  // Try writing a string to a file
  fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  const char buf[] = "Hello, World!\n";
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));
  ASSERT_EQ(close(fd.release()), 0);

  // Re-open as writable
  fd.reset(open(filename, O_WRONLY, 0644));

  // Reading is disallowed
  char tmp[sizeof(buf)];
  ASSERT_EQ(read(fd.get(), tmp, sizeof(tmp)), -1);
  ASSERT_EQ(errno, EBADF);
  errno = 0;

  // Writing is allowed
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));

  // Truncating is allowed
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename), 0);

  END_TEST;
}

bool TestAccessBadFlags() {
  BEGIN_TEST;

  const char* filename = "::foobar";

  fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  // No read-only truncation
  ASSERT_LT(open(filename, O_RDONLY | O_TRUNC | O_CREAT, 0644), 0);

  ASSERT_EQ(unlink(filename), 0);

  END_TEST;
}

bool TestAccessDirectory() {
  BEGIN_TEST;

  const char* filename = "::foobar";

  ASSERT_EQ(mkdir(filename, 0666), 0);

  // Try opening as writable
  fbl::unique_fd fd(open(filename, O_RDWR, 0644));
  ASSERT_FALSE(fd);
  ASSERT_EQ(errno, EISDIR);
  fd.reset(open(filename, O_WRONLY, 0644));
  ASSERT_FALSE(fd);
  ASSERT_EQ(errno, EISDIR);

  // Directories should only be openable with O_RDONLY
  fd.reset(open(filename, O_RDONLY, 0644));
  ASSERT_TRUE(fd);
  fd.reset();

  // Although the directory is opened with O_RDONLY,
  // its subtree should still be writable under POSIX.
  const char* subtree_filename = "::foobar/file";
  fd.reset(open(subtree_filename, O_RDWR | O_TRUNC | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  const char buf[] = "Hello, World!\n";
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));
  ASSERT_EQ(unlink(subtree_filename), 0);

  // Remove the directory we just created
  ASSERT_EQ(rmdir(filename), 0);

  END_TEST;
}

// Fixture setup for hierarchical directory permission tests
class DirectoryPermissionTestFixture {
  // This class creates and tears down a nested structure
  // ::foo/
  //       sub_dir/
  //               sub_file
  //       bar_file
 public:
  DirectoryPermissionTestFixture() {
    ok_ = Setup();
    EXPECT_TRUE(ok_);
  }

  ~DirectoryPermissionTestFixture() { EXPECT_TRUE(Teardown()); }

  bool ok() const { return ok_; }

  bool Setup() {
    BEGIN_HELPER;

    EXPECT_EQ(mkdir("::foo", 0666), 0);
    EXPECT_EQ(mkdir("::foo/sub_dir", 0666), 0);
    int fd = open("::foo/sub_dir/sub_file", O_RDWR | O_TRUNC | O_CREAT, 0644);
    EXPECT_EQ(close(fd), 0);
    fd = open("::foo/bar_file", O_RDWR | O_TRUNC | O_CREAT, 0644);
    EXPECT_EQ(close(fd), 0);

    END_HELPER;
  }

  bool Teardown() {
    BEGIN_HELPER;

    EXPECT_EQ(unlink("::foo/bar_file"), 0);
    EXPECT_EQ(unlink("::foo/sub_dir/sub_file"), 0);
    EXPECT_EQ(rmdir("::foo/sub_dir"), 0);
    EXPECT_EQ(rmdir("::foo"), 0);

    END_HELPER;
  }

 private:
  bool ok_;
};

bool CloneFdAsReadOnlyHelper(fbl::unique_fd in_fd, fbl::unique_fd* out_fd) {
  BEGIN_HELPER;

  // Obtain the underlying connection behind |in_fd|.
  fdio_cpp::FdioCaller fdio_caller(std::move(in_fd));
  zx_handle_t foo_handle = fdio_caller.borrow_channel();

  // Clone |in_fd| as read-only; the entire tree under the new connection now becomes read-only
  zx::channel foo_handle_read_only, foo_request_read_only;
  ASSERT_EQ(zx::channel::create(0, &foo_handle_read_only, &foo_request_read_only), ZX_OK);

  auto clone_result = fio::Node::Call::Clone(
      zx::unowned_channel(foo_handle), fio::OPEN_RIGHT_READABLE, std::move(foo_request_read_only));
  ASSERT_EQ(clone_result.status(), ZX_OK);

  // Turn the handle back to an fd to test posix functions
  fbl::unique_fd fd = ([&]() -> fbl::unique_fd {
    int tmp_fd = -1;
    zx_status_t status = fdio_fd_create(foo_handle_read_only.release(), &tmp_fd);
    EXPECT_GT(tmp_fd, 0);
    EXPECT_EQ(status, ZX_OK);
    return fbl::unique_fd(tmp_fd);
  })();
  ASSERT_TRUE(fd.is_valid());
  *out_fd = std::move(fd);

  END_HELPER;
}

bool TestCloneWithBadFlags() {
  BEGIN_TEST;
  uint32_t rights[] = {
      fio::OPEN_RIGHT_READABLE,
      fio::OPEN_RIGHT_WRITABLE,
      fio::OPEN_RIGHT_ADMIN,
  };

  // CLONE_FLAG_SAME_RIGHTS cannot appear together with any specific rights.
  for (uint32_t right : rights) {
    DirectoryPermissionTestFixture fixture;
    ASSERT_TRUE(fixture.ok());

    fbl::unique_fd foo_fd(open("::foo", O_RDONLY | O_DIRECTORY, 0644));
    ASSERT_GT(foo_fd.get(), 0);

    // Obtain the underlying connection behind |foo_fd|.
    fdio_cpp::FdioCaller fdio_caller(std::move(foo_fd));
    zx_handle_t foo_handle = fdio_caller.borrow_channel();

    zx::channel foo_clone_client_end, foo_clone_server_end;
    ASSERT_EQ(zx::channel::create(0, &foo_clone_client_end, &foo_clone_server_end), ZX_OK);
    auto clone_result =
        fio::Node::Call::Clone(zx::unowned_channel(foo_handle), fio::CLONE_FLAG_SAME_RIGHTS | right,
                               std::move(foo_clone_server_end));
    ASSERT_EQ(clone_result.status(), ZX_OK);
    auto describe_result =
        fio::Node::Call::Describe(zx::unowned_channel(foo_clone_client_end.get()));
    ASSERT_EQ(describe_result.status(), ZX_ERR_PEER_CLOSED);
  }

  END_TEST;
}

bool TestCloneCannotIncreaseRights() {
  BEGIN_TEST;

  {
    DirectoryPermissionTestFixture fixture;
    ASSERT_TRUE(fixture.ok());

    fbl::unique_fd foo_fd(open("::foo", O_RDONLY | O_DIRECTORY, 0644));
    ASSERT_GT(foo_fd.get(), 0);

    fbl::unique_fd foo_readonly;
    ASSERT_TRUE(CloneFdAsReadOnlyHelper(std::move(foo_fd), &foo_readonly));

    // Attempt to clone the read-only fd back to read-write.
    fdio_cpp::FdioCaller fdio_caller(std::move(foo_readonly));
    zx_handle_t foo_handle = fdio_caller.borrow_channel();
    zx::channel foo_clone_client_end, foo_clone_server_end;
    ASSERT_EQ(zx::channel::create(0, &foo_clone_client_end, &foo_clone_server_end), ZX_OK);
    auto clone_result = fio::Node::Call::Clone(zx::unowned_channel(foo_handle),
                                               fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                                               std::move(foo_clone_server_end));
    ASSERT_EQ(clone_result.status(), ZX_OK);
    auto describe_result =
        fio::Node::Call::Describe(zx::unowned_channel(foo_clone_client_end.get()));
    ASSERT_EQ(describe_result.status(), ZX_ERR_PEER_CLOSED);
  }

  END_TEST;
}

bool TestFaccessat() {
  BEGIN_TEST;

  {
    DirectoryPermissionTestFixture fixture;
    ASSERT_TRUE(fixture.ok());

    fbl::unique_fd foo_fd(open("::foo", O_RDONLY | O_DIRECTORY, 0644));
    ASSERT_GT(foo_fd.get(), 0);

    // Verify the tree is read-write
    EXPECT_EQ(faccessat(foo_fd.get(), "bar_file", R_OK | W_OK, 0), 0);
    EXPECT_EQ(faccessat(foo_fd.get(), "sub_dir", R_OK, 0), 0);
    EXPECT_EQ(faccessat(foo_fd.get(), "sub_dir", W_OK, 0), 0);
    EXPECT_EQ(faccessat(foo_fd.get(), "sub_dir", R_OK | W_OK, 0), 0);
    EXPECT_EQ(faccessat(foo_fd.get(), "sub_dir/", R_OK, 0), 0);
    EXPECT_EQ(faccessat(foo_fd.get(), "sub_dir/", W_OK, 0), 0);
    EXPECT_EQ(faccessat(foo_fd.get(), "sub_dir/", R_OK | W_OK, 0), 0);
    EXPECT_EQ(faccessat(foo_fd.get(), "sub_dir/sub_file", R_OK | W_OK, 0), 0);

    fbl::unique_fd rdonly_fd;
    ASSERT_TRUE(CloneFdAsReadOnlyHelper(std::move(foo_fd), &rdonly_fd));

    // Verify the tree is read-only
    EXPECT_EQ(faccessat(rdonly_fd.get(), "bar_file", R_OK, 0), 0);
    EXPECT_EQ(faccessat(rdonly_fd.get(), "bar_file", W_OK, 0), -1);
  }

  END_TEST;
}

bool TestOpathDirectoryAccess() {
  BEGIN_TEST;

  {
    DirectoryPermissionTestFixture fixture;
    ASSERT_TRUE(fixture.ok());

    fbl::unique_fd foo_fd(open("::foo", O_RDONLY | O_DIRECTORY, 0644));
    ASSERT_TRUE(foo_fd.is_valid());

    // If sub_dir is opened with O_PATH,
    // it should not be possible to open sub_file from there as O_RDWR,
    // because Fuchsia's O_PATH disallows this explicitly
    fbl::unique_fd sub_dir_fd(openat(foo_fd.get(), "sub_dir", O_PATH, 0644));
    ASSERT_TRUE(sub_dir_fd.is_valid());

    fbl::unique_fd sub_file_fd(openat(sub_dir_fd.get(), "sub_file", O_RDWR, 0644));
    ASSERT_FALSE(sub_file_fd.is_valid());
  }

  END_TEST;
}

bool TestRestrictDirectoryAccess() {
  BEGIN_TEST;

  {
    DirectoryPermissionTestFixture fixture;
    ASSERT_TRUE(fixture.ok());

    // Open ::foo and get the underlying connection
    fbl::unique_fd foo_fd(open("::foo", O_RDONLY | O_DIRECTORY, 0644));
    ASSERT_GT(foo_fd.get(), 0);

    fbl::unique_fd rdonly_fd;
    ASSERT_TRUE(CloneFdAsReadOnlyHelper(std::move(foo_fd), &rdonly_fd));

    // Verify the tree is read-only
    int bar_file_fd = openat(rdonly_fd.get(), "bar_file", O_RDONLY, 0644);
    ASSERT_GT(bar_file_fd, 0);
    ASSERT_EQ(close(bar_file_fd), 0);

    bar_file_fd = openat(rdonly_fd.get(), "bar_file", O_RDWR, 0644);
    ASSERT_LT(bar_file_fd, 0);
    ASSERT_EQ(errno, EACCES);

    int sub_file_fd = openat(rdonly_fd.get(), "sub_dir/sub_file", O_RDONLY, 0644);
    ASSERT_GT(sub_file_fd, 0);
    ASSERT_EQ(close(sub_file_fd), 0);

    sub_file_fd = openat(rdonly_fd.get(), "sub_dir/sub_file", O_RDWR, 0644);
    ASSERT_LT(sub_file_fd, 0);
    ASSERT_EQ(errno, EACCES);
  }

  END_TEST;
}

bool TestModifyingFileTime() {
  BEGIN_TEST;

  struct timespec ts[2] = {};
  ASSERT_EQ(clock_gettime(CLOCK_REALTIME, &ts[0]), 0);
  ASSERT_EQ(clock_gettime(CLOCK_REALTIME, &ts[1]), 0);

  {
    DirectoryPermissionTestFixture fixture;
    ASSERT_TRUE(fixture.ok());

    // Open ::foo; it will be read-write.
    fbl::unique_fd foo_fd(open("::foo", O_RDONLY | O_DIRECTORY, 0644));
    ASSERT_GT(foo_fd.get(), 0);
    // futimens on foo_fd is allowed because it is writable
    ASSERT_EQ(futimens(foo_fd.get(), ts), 0);
    // utimensat on bar_file is allowed because the parent is writable
    ASSERT_EQ(utimensat(foo_fd.get(), "bar_file", ts, 0), 0);
    // utimensat on sub_dir is allowed because the parent is writable
    ASSERT_EQ(utimensat(foo_fd.get(), "sub_dir", ts, 0), 0);
    ASSERT_EQ(utimensat(foo_fd.get(), "sub_dir/", ts, 0), 0);

    // Clone foo_fd it as read-only.
    fbl::unique_fd rdonly_fd;
    ASSERT_TRUE(CloneFdAsReadOnlyHelper(std::move(foo_fd), &rdonly_fd));

    // futimens on the read-only clone is not allowed
    ASSERT_LT(futimens(rdonly_fd.get(), ts), 0);
    // utimensat on bar_file is not allowed because the parent is read-only
    ASSERT_LT(utimensat(rdonly_fd.get(), "bar_file", ts, 0), 0);
    // utimensat on sub_dir is not allowed because the parent is read-only
    ASSERT_LT(utimensat(rdonly_fd.get(), "sub_dir", ts, 0), 0);
    ASSERT_LT(utimensat(rdonly_fd.get(), "sub_dir/", ts, 0), 0);
    // futimens on bar_file is not allowed because it requires write access
    int bar_file_fd = openat(rdonly_fd.get(), "bar_file", O_RDONLY, 0644);
    ASSERT_GT(bar_file_fd, 0);
    ASSERT_LT(futimens(bar_file_fd, ts), 0);
    ASSERT_EQ(close(bar_file_fd), 0);
  }

  END_TEST;
}

bool TestAccessOpath() {
  BEGIN_TEST;

  const char* dirname = "::foo";
  const char* filename = "::foo/bar";

  ASSERT_EQ(mkdir(dirname, 0666), 0);

  // Cannot create a file as O_PATH
  fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR | O_PATH));
  ASSERT_FALSE(fd);

  const char* data = "hello";
  const size_t datalen = strlen(data);

  fd.reset(open(filename, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), data, datalen), static_cast<ssize_t>(datalen));
  ASSERT_EQ(close(fd.release()), 0);

  // Cannot read to / write from O_PATH fd
  fd.reset(open(filename, O_RDWR | O_PATH));
  ASSERT_TRUE(fd);

  char buf[128];
  ASSERT_LT(read(fd.get(), buf, sizeof(buf)), 0);
  ASSERT_EQ(errno, EBADF);
  ASSERT_LT(write(fd.get(), data, datalen), 0);
  ASSERT_EQ(errno, EBADF);
  ASSERT_LT(lseek(fd.get(), 1, SEEK_SET), 0);
  ASSERT_EQ(errno, EBADF);

  // We can fstat the file, however
  struct stat st;
  ASSERT_EQ(fstat(fd.get(), &st), 0);
  ASSERT_EQ(st.st_size, static_cast<ssize_t>(datalen));
  ASSERT_EQ(close(fd.release()), 0);

  // We can pass in a variety of flags to open with O_PATH, and
  // they'll be ignored.
  fd.reset(open(filename, O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_PATH));
  ASSERT_TRUE(fd);
  ASSERT_EQ(fstat(fd.get(), &st), 0);
  ASSERT_EQ(st.st_size, static_cast<ssize_t>(datalen));

  // We can use fcntl on the fd
  int flags = fcntl(fd.get(), F_GETFL);
  ASSERT_GT(flags, -1);
  ASSERT_EQ(flags & O_ACCMODE, O_PATH);
  ASSERT_EQ(flags & ~O_ACCMODE, 0);

  // We can toggle some flags, even if they don't make much sense
  ASSERT_EQ(fcntl(fd.get(), F_SETFL, flags | O_APPEND), 0);
  flags = fcntl(fd.get(), F_GETFL);
  ASSERT_EQ(flags & O_ACCMODE, O_PATH);
  ASSERT_EQ(flags & ~O_ACCMODE, O_APPEND);
  // We still can't write though
  ASSERT_LT(write(fd.get(), data, datalen), 0);
  ASSERT_EQ(errno, EBADF);

  // We cannot update attributes of the file
  struct timespec ts[2];
  ts[0].tv_nsec = UTIME_OMIT;
  ts[1].tv_sec = 0;
  ts[1].tv_nsec = 0;
  ASSERT_LT(futimens(fd.get(), ts), 0);
  ASSERT_EQ(errno, EBADF);
  ASSERT_EQ(close(fd.release()), 0);

  // O_PATH doesn't ignore O_DIRECTORY
  ASSERT_LT(open(filename, O_PATH | O_DIRECTORY), 0);

  // We can use O_PATH when opening directories too
  fd.reset(open(dirname, O_PATH | O_DIRECTORY));
  ASSERT_TRUE(fd);

  // The *at functions are not allowed on Fuchsia, for an O_PATH-opened directory.
  ASSERT_LT(renameat(fd.get(), "bar", fd.get(), "baz"), 0);
  ASSERT_EQ(errno, EBADF);

  // Readdir is not allowed
  DIR* dir = fdopendir(fd.get());
  ASSERT_NONNULL(dir);
  struct dirent* de = readdir(dir);
  ASSERT_NULL(de);
  ASSERT_EQ(errno, EBADF);
  ASSERT_EQ(closedir(dir), 0);

  ASSERT_EQ(unlink(filename), 0);
  ASSERT_EQ(rmdir(dirname), 0);

  END_TEST;
}

// This test case was created to prevent a regression of a
// file descriptor refcounting bug: files opened with "O_PATH"
// do not cause the underlying object to be opened, and files
// opened without "O_PATH" do cause the underlying object to
// be opened. Cloning the object should not invalidate the
// internal file descriptor count.
bool TestOpathFdCount() {
  BEGIN_TEST;

  const char* dirname = "::foo";
  fbl::unique_fd fd(-1);
  zx_handle_t handle = ZX_HANDLE_INVALID;

  // Opened with O_PATH, cloned, and closed before clone.
  ASSERT_EQ(mkdir(dirname, 0666), 0);
  fd.reset(open(dirname, O_PATH | O_DIRECTORY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(fdio_fd_clone(fd.get(), &handle), ZX_OK);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(zx_handle_close(handle), ZX_OK);
  ASSERT_EQ(rmdir(dirname), 0);

  // Opened with O_PATH, cloned, and closed after clone.
  ASSERT_EQ(mkdir(dirname, 0666), 0);
  fd.reset(open(dirname, O_PATH | O_DIRECTORY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(fdio_fd_clone(fd.get(), &handle), ZX_OK);
  ASSERT_EQ(zx_handle_close(handle), ZX_OK);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(rmdir(dirname), 0);

  END_TEST;
}

}  // namespace

RUN_FOR_ALL_FILESYSTEMS(access_tests,
                        RUN_TEST_SMALL(TestAccessReadable) RUN_TEST_SMALL(TestAccessWritable)
                            RUN_TEST_SMALL(TestAccessBadFlags) RUN_TEST_SMALL(TestAccessDirectory)
                                RUN_TEST_SMALL(TestCloneWithBadFlags)
                                    RUN_TEST_SMALL(TestCloneCannotIncreaseRights)
                                        RUN_TEST_SMALL(TestFaccessat)
                                            RUN_TEST_SMALL(TestOpathDirectoryAccess)
                                                RUN_TEST_SMALL(TestRestrictDirectoryAccess)
                                                    RUN_TEST_SMALL(TestModifyingFileTime)
                                                        RUN_TEST_SMALL(TestAccessOpath)
                                                            RUN_TEST_SMALL(TestOpathFdCount))
