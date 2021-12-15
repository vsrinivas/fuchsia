// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/defer.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

// These tests validate basic cross-platform usage of mmap.
//
// More comprehensive tests configured to run across all Fuchsia filesystems can be found in
// `src/storage/fs_test/mmap.cc`.

namespace {

const size_t kPageSize{static_cast<size_t>(sysconf(_SC_PAGESIZE))};

const std::string kTempPathTemplate{"/tmp/fdio_mmap_test.XXXXXX"};

/// Helper function that returns a RAII object that wraps a pointer obtained by mmap so that it
/// is automatically unmapped (via `munmap`) when it goes out of scope. Will cause test failure
/// if the region could not be unmapped, but does not stop a test from continuing.
auto defer_munmap(void* addr, size_t size) {
  EXPECT_NE(addr, nullptr);
  EXPECT_GT(size, 0);
  return fit::defer([addr, size]() {
    EXPECT_EQ(munmap(addr, size), 0, "Could not unmap memory (ptr=%p, size=%zu): %s", addr, size,
              strerror(errno));
  });
}

template <int fd_flags>
class MmapTestFixture : public zxtest::Test {
 protected:
  /// Return the file descriptor that was opened for the current test case.
  int fd() const { return fd_.get(); }

  void SetUp() override {
    std::string temp_path{kTempPathTemplate};
    // Create a temporary file/directory based on fd_flags.
    if (fd_flags & O_DIRECTORY) {
      ASSERT_NE(mkdtemp(temp_path.data()), nullptr, "%s", strerror(errno));
    } else {
      ASSERT_TRUE(fd_ = fbl::unique_fd{mkstemp(temp_path.data())}, "%s", strerror(errno));
    }
    // Re-open it with the specified flags, and remove/unlink it from disk.
    EXPECT_TRUE(fd_ = fbl::unique_fd{open(temp_path.c_str(), fd_flags)}, "%s", strerror(errno));
    ASSERT_EQ(remove(temp_path.c_str()), 0, "%s", strerror(errno));
  }

 private:
  fbl::unique_fd fd_;
};

using MmapTest = MmapTestFixture<O_RDWR>;
using MmapTestReadOnly = MmapTestFixture<O_RDONLY>;
using MmapTestWriteOnly = MmapTestFixture<O_WRONLY>;
using MmapTestDirectory = MmapTestFixture<O_RDONLY | O_DIRECTORY>;

// Test that MAP_PRIVATE keeps all copies of the underlying buffer separate, thus
// ensuring copy-on-write semantics.
TEST_F(MmapTest, Private) {
  const char data_1[] = "Hello, world!";
  const char data_2[] = "Other data...";
  constexpr size_t data_len = sizeof(data_1);
  static_assert(sizeof(data_1) == sizeof(data_2));  // Size must match for this test.

  // Initialize a file with some data.
  ASSERT_EQ(write(fd(), data_1, data_len), data_len, "%s", strerror(errno));

  // Ensure that we can mmap it and it's contents match.
  void* map_1;
  ASSERT_NE(map_1 = mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd(), 0),
            MAP_FAILED, "%s", strerror(errno));
  auto unmap_1 = defer_munmap(map_1, kPageSize);
  EXPECT_BYTES_EQ(static_cast<char*>(map_1), data_1, data_len);

  // Do another private mapping, but modify the data that was mapped.
  void* map_2;
  ASSERT_NE(map_2 = mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd(), 0),
            MAP_FAILED, "%s", strerror(errno));
  auto unmap_2 = defer_munmap(map_2, kPageSize);
  EXPECT_BYTES_EQ(static_cast<char*>(map_2), data_1, data_len);
  memcpy(map_2, data_2, data_len);
  // Ensure the original mapping was not modified.
  EXPECT_BYTES_EQ(static_cast<char*>(map_1), data_1, data_len);
  EXPECT_BYTES_EQ(static_cast<char*>(map_2), data_2, data_len);
}

// Test that file writes are propagated to a shared read-only buffer.
TEST_F(MmapTest, Shared) {
  const char data_1[] = "this is a buffer";
  const char data_2[] = "another buffer!!";
  constexpr size_t data_len = sizeof(data_1);
  static_assert(sizeof(data_1) == sizeof(data_2));  // Size must match for this test.

  // Initialize a file with some data.
  ASSERT_EQ(write(fd(), data_1, data_len), data_len, "%s", strerror(errno));

  // Demonstrate that a simple read-only buffer can be mapped.
  void* addr;
  ASSERT_NE(addr = mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED, fd(), 0), MAP_FAILED, "%s",
            strerror(errno));
  auto unmap = defer_munmap(addr, kPageSize);
  EXPECT_BYTES_EQ(static_cast<char*>(addr), data_1, data_len);

  // Show that if we overwrite part of the file, the mapping is also updated.
  ASSERT_EQ(lseek(fd(), 0, SEEK_SET), 0, "%s", strerror(errno));
  ASSERT_EQ(write(fd(), data_2, data_len), data_len, "%s", strerror(errno));
  EXPECT_BYTES_EQ(static_cast<char*>(addr), data_2, data_len);
}

// Test that we fail to mmap an fd that does not support it.
TEST_F(MmapTestDirectory, FailUnsupported) {
  // Try to mmap a directory, which should fail.
  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, ENODEV, "%s", strerror(errno));
}

// Test that calling mmap with an unaligned offset fails (the offset must be page aligned).
TEST_F(MmapTest, UnalignedOffset) {
  constexpr off_t kUnalignedOffset = 1;  // Must *not* be page aligned for this test.
  ASSERT_TRUE(kUnalignedOffset % kPageSize, "Offset must not be aligned to page size!");
  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED, fd(), kUnalignedOffset), MAP_FAILED);
  EXPECT_EQ(errno, EINVAL, "%s", strerror(errno));
}

// Test that calling mmap with a length of zero fails.
TEST_F(MmapTest, ZeroLength) {
  ASSERT_EQ(mmap(nullptr, 0, PROT_READ, MAP_SHARED, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EINVAL, "%s", strerror(errno));
}

// Test cases where certain combinations of MAP_* flags are required to fail with EINVAL.
TEST_F(MmapTest, InvalidMapFlags) {
  // `mmap` without either MAP_PRIVATE or MAP_SHARED should fail.
  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ, 0, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EINVAL, "%s", strerror(errno));

  // `mmap` with both MAP_PRIVATE and MAP_SHARED *should* fail, but the introduction of the
  // MAP_SHARED_VALIDATE extension in Linux [1] causes this to succeed due to overlapping bit masks.
  // Although the POSIX standard requires this case return EINVAL [2], it no longer does on Linux
  // due to MAP_SHARED | MAP_PRIVATE being equal to MAP_SHARED_VALIDATE [3].
  //
  // A patch to revert this behavior was proposed but rejected [4][5], thus we do not test this
  // combination of flags on systems where MAP_SHARED_VALIDATE exists. Where it does exist, we
  // ensure it still overlaps with MAP_SHARED | MAP_PRIVATE should this assumption be broken in the
  // future.  We also trigger a static assertion should this flag be added to Fuchsia in the future
  // so that this test case can be updated accordingly.
  //
  // [1]: https://github.com/torvalds/linux/commit/1c9725974074a047f6080eecc62c50a8e840d050
  // [2]: https://pubs.opengroup.org/onlinepubs/9699919799/functions/mmap.html
  // [3]: https://git.kernel.org/pub/scm/docs/man-pages/man-pages.git/commit/?id=c4f0c33fb6
  // [4]: https://lwn.net/Articles/758594/
  // [5]: https://lwn.net/Articles/758597/
#if defined(__Fuchsia__)
#if defined(MAP_SHARED_VALIDATE)
  static_assert(false, "Test case must be updated if MAP_SHARED_VALIDATE is present on Fuchsia!");
#endif
  // Verify that Fuchsia complies with the behavior specified in the POSIX standard.
  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED | MAP_PRIVATE, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EINVAL, "%s", strerror(errno));
#else
#if defined(MAP_SHARED_VALIDATE)
  // In the event these flags no longer overlap, we need to revisit if MAP_SHARED | MAP_PRIVATE
  // will correctly match the behavior in the POSIX standard (being rejected with EINVAL).
  static_assert(MAP_SHARED_VALIDATE == (MAP_SHARED | MAP_PRIVATE),
                "MAP_SHARED_VALIDATE no longer overlaps MAP_SHARED | MAP_PRIVATE! Test case "
                "requires update.");
#endif
  // Ensure that if the mapping does fail, the correct error code is returned.
  void* addr = mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED | MAP_PRIVATE, fd(), 0);
  if (addr == MAP_FAILED) {
    EXPECT_EQ(errno, EINVAL, "%s", strerror(errno));
  } else {
    auto unmap = defer_munmap(addr, kPageSize);
  }
#endif
}

// Test all cases of MAP_PRIVATE and MAP_SHARED which require a readable file (fd is write-only).
TEST_F(MmapTestWriteOnly, AccessDenied) {
  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ, MAP_PRIVATE, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));

  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_WRITE, MAP_PRIVATE, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));

  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));

  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ, MAP_SHARED, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));

  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_WRITE, MAP_SHARED, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));

  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));
}

// Test all cases of MAP_PRIVATE and MAP_SHARED which require a writable file (fd is read only).
// Note that MAP_PRIVATE never requires a writable file, since it makes a COW clone.
TEST_F(MmapTestReadOnly, AccessDenied) {
  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_WRITE, MAP_SHARED, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));

  ASSERT_EQ(mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd(), 0), MAP_FAILED);
  EXPECT_EQ(errno, EACCES, "%s", strerror(errno));
}

}  // namespace
