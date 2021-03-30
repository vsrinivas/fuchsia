// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/inotify.h>
#include <limits.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(InotifyInit, BadFlags) {
  ASSERT_EQ(inotify_init1(5), -1);
  ASSERT_ERRNO(EINVAL);
}

class Inotify : public zxtest::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(fd_ = fbl::unique_fd(inotify_init1(0)), "%s", strerror(errno));
  }

  const fbl::unique_fd& fd() { return fd_; }

 private:
  fbl::unique_fd fd_;
};

TEST_F(Inotify, AddWatch) { ASSERT_GE(inotify_add_watch(fd().get(), "/path/test", IN_CREATE), 0); }

TEST_F(Inotify, AddWatchWithNullFilePath) {
  ASSERT_EQ(inotify_add_watch(fd().get(), nullptr, 0), -1);
  ASSERT_ERRNO(EFAULT);
}

TEST_F(Inotify, AddWatchWithZeroLengthFilePath) {
  ASSERT_EQ(inotify_add_watch(fd().get(), "", 0), -1);
  ASSERT_ERRNO(EINVAL);
}

TEST_F(Inotify, AddWatchWithTooLongFilePath) {
  std::string long_filepath(PATH_MAX + 1, 'x');
  ASSERT_EQ(inotify_add_watch(fd().get(), long_filepath.c_str(), 0), -1);
  ASSERT_ERRNO(ENAMETOOLONG);
}

class InotifyWatch : public Inotify {
 protected:
  void SetUp() override {
    Inotify::SetUp();
    ASSERT_GE(wd_ = inotify_add_watch(fd().get(), "/path/test", IN_CREATE), 0, "%s",
              strerror(errno));
  }

  int wd() const { return wd_; }

 private:
  int wd_;
};

TEST_F(InotifyWatch, Remove) { ASSERT_SUCCESS(inotify_rm_watch(fd().get(), wd())); }

TEST_F(InotifyWatch, RemoveWithInvalidInotifyDescriptor) {
  ASSERT_EQ(inotify_rm_watch(fd().get() + 1, wd()), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(InotifyWatch, RemoveWithInvalidWatchDescriptor) {
  ASSERT_EQ(inotify_rm_watch(fd().get(), wd() + 1), -1);
  ASSERT_ERRNO(EINVAL);
}
