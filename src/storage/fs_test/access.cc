// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

namespace fio = fuchsia_io;

using AccessTest = FilesystemTest;

TEST_P(AccessTest, ReadOnlyFileIsImmutable) {
  const std::string filename = GetPath("alpha");

  // Try writing a string to a file
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  const char buf[] = "Hello, World!\n";
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf))) << errno;
  ASSERT_EQ(close(fd.release()), 0);

  // Re-open as readonly
  fd.reset(open(filename.c_str(), O_RDONLY, 0644));

  // Reading is allowed
  char tmp[sizeof(buf)];
  ASSERT_EQ(read(fd.get(), tmp, sizeof(tmp)), static_cast<ssize_t>(sizeof(tmp)));
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
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

TEST_P(AccessTest, WriteOnlyIsNotReadable) {
  const std::string filename = GetPath("alpha");

  // Try writing a string to a file
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  const char buf[] = "Hello, World!\n";
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_EQ(close(fd.release()), 0);

  // Re-open as writable
  fd.reset(open(filename.c_str(), O_WRONLY, 0644));

  // Reading is disallowed
  char tmp[sizeof(buf)];
  ASSERT_EQ(read(fd.get(), tmp, sizeof(tmp)), -1);
  ASSERT_EQ(errno, EBADF);
  errno = 0;

  // Writing is allowed
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));

  // Truncating is allowed
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

TEST_P(AccessTest, OpenFileWithTruncateAndReadOnlyIsError) {
  const std::string filename = GetPath("foobar");

  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  // No read-only truncation
  ASSERT_LT(open(filename.c_str(), O_RDONLY | O_TRUNC | O_CREAT, 0644), 0);

  ASSERT_EQ(unlink(filename.c_str()), 0);
}

TEST_P(AccessTest, TestAccessDirectory) {
  const std::string filename = GetPath("foobar");

  ASSERT_EQ(mkdir(filename.c_str(), 0666), 0);

  // Try opening as writable
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR, 0644));
  ASSERT_FALSE(fd);
  ASSERT_EQ(errno, EISDIR);
  fd.reset(open(filename.c_str(), O_WRONLY, 0644));
  ASSERT_FALSE(fd);
  ASSERT_EQ(errno, EISDIR);

  // Directories should only be openable with O_RDONLY
  fd.reset(open(filename.c_str(), O_RDONLY, 0644));
  ASSERT_TRUE(fd);
  fd.reset();

  // Although the directory is opened with O_RDONLY,
  // its subtree should still be writable under POSIX.
  const std::string subtree_filename = GetPath("foobar/file");
  fd.reset(open(subtree_filename.c_str(), O_RDWR | O_TRUNC | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  const char buf[] = "Hello, World!\n";
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_EQ(unlink(subtree_filename.c_str()), 0);

  // Remove the directory we just created
  ASSERT_EQ(rmdir(filename.c_str()), 0);
}

// Same as the previous test, but open the directory first to guarantee caching comes into play.
TEST_P(AccessTest, TestAccessDirectoryCache) {
  const std::string filename = GetPath("foobar");
  ASSERT_EQ(mkdir(filename.c_str(), 0666), 0);

  auto read_fd = fbl::unique_fd(open(filename.c_str(), O_RDONLY));
  ASSERT_TRUE(read_fd);

  // Try opening as writable
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR, 0644));
  ASSERT_FALSE(fd);
  ASSERT_EQ(errno, EISDIR);
  fd.reset(open(filename.c_str(), O_WRONLY, 0644));
  ASSERT_FALSE(fd);
  ASSERT_EQ(errno, EISDIR);
}

// Fixture setup for hierarchical directory permission tests
class DirectoryPermissionTest : public AccessTest {
  // This class creates and tears down a nested structure
  // ::foo/
  //       sub_dir/
  //               sub_file
  //       bar_file
 public:
  DirectoryPermissionTest() {
    EXPECT_EQ(mkdir(GetPath("foo").c_str(), 0666), 0);
    EXPECT_EQ(mkdir(GetPath("foo/sub_dir").c_str(), 0666), 0);
    int fd = open(GetPath("foo/sub_dir/sub_file").c_str(), O_RDWR | O_TRUNC | O_CREAT, 0644);
    EXPECT_EQ(close(fd), 0);
    fd = open(GetPath("foo/bar_file").c_str(), O_RDWR | O_TRUNC | O_CREAT, 0644);
    EXPECT_EQ(close(fd), 0);
  }

  ~DirectoryPermissionTest() override {
    EXPECT_EQ(unlink(GetPath("foo/bar_file").c_str()), 0);
    EXPECT_EQ(unlink(GetPath("foo/sub_dir/sub_file").c_str()), 0);
    EXPECT_EQ(rmdir(GetPath("foo/sub_dir").c_str()), 0);
    EXPECT_EQ(rmdir(GetPath("foo").c_str()), 0);
  }
};

void CloneFdAsReadOnlyHelper(fbl::unique_fd in_fd, fbl::unique_fd* out_fd) {
  // Obtain the underlying connection behind |in_fd|.
  fdio_cpp::FdioCaller fdio_caller(std::move(in_fd));

  // Clone |in_fd| as read-only; the entire tree under the new connection now becomes read-only
  zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  auto clone_result =
      fidl::WireCall(fdio_caller.borrow_as<fio::Node>())
          ->Clone(fio::wire::OpenFlags::kRightReadable, std::move(std::move(endpoints->server)));
  ASSERT_EQ(clone_result.status(), ZX_OK);

  // Turn the handle back to an fd to test posix functions
  fbl::unique_fd fd = ([&]() -> fbl::unique_fd {
    int tmp_fd = -1;
    zx_status_t status = fdio_fd_create(endpoints->client.TakeChannel().release(), &tmp_fd);
    EXPECT_GT(tmp_fd, 0);
    EXPECT_EQ(status, ZX_OK);
    return fbl::unique_fd(tmp_fd);
  })();
  ASSERT_TRUE(fd.is_valid());
  *out_fd = std::move(fd);
}

TEST_P(DirectoryPermissionTest, TestCloneWithBadFlags) {
  fio::wire::OpenFlags rights[] = {
      fio::wire::OpenFlags::kRightReadable,
      fio::wire::OpenFlags::kRightWritable,
  };

  // CLONE_FLAG_SAME_RIGHTS cannot appear together with any specific rights.
  for (fio::wire::OpenFlags right : rights) {
    fbl::unique_fd foo_fd(open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644));
    ASSERT_GT(foo_fd.get(), 0);

    // Obtain the underlying connection behind |foo_fd|.
    fdio_cpp::FdioCaller fdio_caller(std::move(foo_fd));

    zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
    ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

    auto clone_result =
        fidl::WireCall(fdio_caller.borrow_as<fio::Node>())
            ->Clone(fio::wire::OpenFlags::kCloneSameRights | right, std::move(endpoints->server));
    ASSERT_EQ(clone_result.status(), ZX_OK);
    auto describe_result = fidl::WireCall(endpoints->client)->Query();
    ASSERT_EQ(describe_result.status(), ZX_ERR_PEER_CLOSED);
  }
}

TEST_P(DirectoryPermissionTest, TestCloneCannotIncreaseRights) {
  fbl::unique_fd foo_fd(open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_GT(foo_fd.get(), 0);

  fbl::unique_fd foo_readonly;
  CloneFdAsReadOnlyHelper(std::move(foo_fd), &foo_readonly);

  // Attempt to clone the read-only fd back to read-write.
  fdio_cpp::FdioCaller fdio_caller(std::move(foo_readonly));

  zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();

  auto clone_result =
      fidl::WireCall(fdio_caller.borrow_as<fio::Node>())
          ->Clone(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable,
                  std::move(endpoints->server));
  ASSERT_EQ(clone_result.status(), ZX_OK);
  auto describe_result = fidl::WireCall(endpoints->client)->Query();
  ASSERT_EQ(describe_result.status(), ZX_ERR_PEER_CLOSED);
}

TEST_P(DirectoryPermissionTest, TestFaccessat) {
  fbl::unique_fd foo_fd(open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644));
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
  CloneFdAsReadOnlyHelper(std::move(foo_fd), &rdonly_fd);

  // Verify the tree is read-only
  EXPECT_EQ(faccessat(rdonly_fd.get(), "bar_file", R_OK, 0), 0);
  EXPECT_EQ(faccessat(rdonly_fd.get(), "bar_file", W_OK, 0), -1);
}

TEST_P(DirectoryPermissionTest, TestOpathDirectoryAccess) {
  fbl::unique_fd foo_fd(open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_TRUE(foo_fd.is_valid());

  // If sub_dir is opened with O_PATH,
  // it should not be possible to open sub_file from there as O_RDWR,
  // because Fuchsia's O_PATH disallows this explicitly
  fbl::unique_fd sub_dir_fd(openat(foo_fd.get(), "sub_dir", O_PATH, 0644));
  ASSERT_TRUE(sub_dir_fd.is_valid());

  fbl::unique_fd sub_file_fd(openat(sub_dir_fd.get(), "sub_file", O_RDWR, 0644));
  ASSERT_FALSE(sub_file_fd.is_valid());
}

TEST_P(DirectoryPermissionTest, TestRestrictDirectoryAccess) {
  // Open ::foo and get the underlying connection
  fbl::unique_fd foo_fd(open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_GT(foo_fd.get(), 0);

  fbl::unique_fd rdonly_fd;
  CloneFdAsReadOnlyHelper(std::move(foo_fd), &rdonly_fd);

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

TEST_P(DirectoryPermissionTest, TestModifyingFileTime) {
  struct timespec ts[2] = {};
  ASSERT_EQ(clock_gettime(CLOCK_REALTIME, &ts[0]), 0);
  ASSERT_EQ(clock_gettime(CLOCK_REALTIME, &ts[1]), 0);

  // Open ::foo; it will be read-write.
  fbl::unique_fd foo_fd(open(GetPath("foo").c_str(), O_RDONLY | O_DIRECTORY, 0644));
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
  CloneFdAsReadOnlyHelper(std::move(foo_fd), &rdonly_fd);

  // futimens on the read-only clone is not allowed
  ASSERT_LT(futimens(rdonly_fd.get(), ts), 0);
  ASSERT_EQ(errno, EBADF);
  // utimensat on bar_file is not allowed because the parent is read-only
  ASSERT_LT(utimensat(rdonly_fd.get(), "bar_file", ts, 0), 0);
  ASSERT_EQ(errno, EACCES);
  // utimensat on sub_dir is not allowed because the parent is read-only
  ASSERT_LT(utimensat(rdonly_fd.get(), "sub_dir", ts, 0), 0);
  ASSERT_EQ(errno, EACCES);
  ASSERT_LT(utimensat(rdonly_fd.get(), "sub_dir/", ts, 0), 0);
  ASSERT_EQ(errno, EACCES);
  // futimens on bar_file is not allowed because it requires write access
  int bar_file_fd = openat(rdonly_fd.get(), "bar_file", O_RDONLY, 0644);
  ASSERT_GT(bar_file_fd, 0);
  ASSERT_LT(futimens(bar_file_fd, ts), 0);
  ASSERT_EQ(errno, EBADF);
  ASSERT_EQ(close(bar_file_fd), 0);
}

TEST_P(AccessTest, TestAccessOpath) {
  const std::string dirname = GetPath("foo");
  const std::string filename = GetPath("foo/bar");

  ASSERT_EQ(mkdir(dirname.c_str(), 0666), 0);

  // Cannot create a file as O_PATH
  fbl::unique_fd fd(open(filename.c_str(), O_CREAT | O_RDWR | O_PATH, S_IRUSR | S_IWUSR));
  ASSERT_FALSE(fd);

  const char* data = "hello";
  const size_t datalen = strlen(data);

  fd.reset(open(filename.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), data, datalen), static_cast<ssize_t>(datalen));
  ASSERT_EQ(close(fd.release()), 0);

  // Cannot read to / write from O_PATH fd
  fd.reset(open(filename.c_str(), O_RDWR | O_PATH));
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
  fd.reset(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_PATH, S_IRUSR | S_IWUSR));
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
  ASSERT_LT(open(filename.c_str(), O_PATH | O_DIRECTORY), 0);

  // We can use O_PATH when opening directories too
  fd.reset(open(dirname.c_str(), O_PATH | O_DIRECTORY));
  ASSERT_TRUE(fd);

  // The *at functions are not allowed on Fuchsia, for an O_PATH-opened directory.
  ASSERT_LT(renameat(fd.get(), "bar", fd.get(), "baz"), 0);
  ASSERT_EQ(errno, EBADF);

  // Readdir is not allowed
  DIR* dir = fdopendir(fd.get());
  ASSERT_NE(dir, nullptr);
  struct dirent* de = readdir(dir);
  ASSERT_EQ(de, nullptr);
  ASSERT_EQ(errno, EBADF);
  ASSERT_EQ(closedir(dir), 0);

  ASSERT_EQ(unlink(filename.c_str()), 0);
  ASSERT_EQ(rmdir(dirname.c_str()), 0);
}

// This test case was created to prevent a regression of a
// file descriptor refcounting bug: files opened with "O_PATH"
// do not cause the underlying object to be opened, and files
// opened without "O_PATH" do cause the underlying object to
// be opened. Cloning the object should not invalidate the
// internal file descriptor count.
TEST_P(AccessTest, TestOpathFdCount) {
  const std::string dirname = GetPath("foo");
  fbl::unique_fd fd(-1);
  zx_handle_t handle = ZX_HANDLE_INVALID;

  // Opened with O_PATH, cloned, and closed before clone.
  ASSERT_EQ(mkdir(dirname.c_str(), 0666), 0);
  fd.reset(open(dirname.c_str(), O_PATH | O_DIRECTORY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(fdio_fd_clone(fd.get(), &handle), ZX_OK);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(zx_handle_close(handle), ZX_OK);
  ASSERT_EQ(rmdir(dirname.c_str()), 0);

  // Opened with O_PATH, cloned, and closed after clone.
  ASSERT_EQ(mkdir(dirname.c_str(), 0666), 0);
  fd.reset(open(dirname.c_str(), O_PATH | O_DIRECTORY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(fdio_fd_clone(fd.get(), &handle), ZX_OK);
  ASSERT_EQ(zx_handle_close(handle), ZX_OK);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(rmdir(dirname.c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, AccessTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, DirectoryPermissionTest,
                         testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
