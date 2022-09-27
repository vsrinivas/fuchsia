// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/elfldltl/mapped-fd-file.h>
#include <lib/fit/defer.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

constexpr std::string_view kContents = "file contents";

TEST(ElfldltlMappedFdFileTests, Basic) {
  FILE* f = tmpfile();
  ASSERT_NOT_NULL(f);
  auto close_f = fit::defer([f]() { EXPECT_EQ(0, fclose(f)); });

  ASSERT_EQ(kContents.size(), fwrite(kContents.data(), 1, kContents.size(), f));

  // Flushing ensures the file size is updated before Init.
  ASSERT_EQ(0, fflush(f));

  elfldltl::MappedFdFile fdfile;
  ASSERT_TRUE(fdfile.Init(fileno(f)).is_ok());

  // Closing the fd does not affect reading later.
  close_f.call();

  {
    // Test move-construction and move-assignment.
    elfldltl::MappedFdFile moved_fdfile(std::move(fdfile));
    fdfile = std::move(moved_fdfile);
  }

  auto res = fdfile.ReadArrayFromFile<char>(0, elfldltl::NoArrayFromFile<char>(), kContents.size());
  ASSERT_TRUE(res);

  std::string_view sv{res->data(), res->size()};
  EXPECT_EQ(sv, kContents);
}

TEST(ElfldltlMappedFdFileTests, BadFd) {
  elfldltl::MappedFdFile fdfile;
  auto result = fdfile.Init(-1);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), EBADF);
}

TEST(ElfldltlMappedFdFileTests, NotFile) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  fbl::unique_fd rfd(fds[0]), wfd(fds[1]);

  elfldltl::MappedFdFile fdfile;
  auto result = fdfile.Init(rfd.get());
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ENOTSUP);
}

// There's no easy way to test for a valid but un-mmap-able file,
// nor for munmap failure.

}  // namespace
