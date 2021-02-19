// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/inotify.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(InotifyTest, InotifyInitBadFlags) {
  int inotifyfd = inotify_init1(5);
  ASSERT_EQ(inotifyfd, -1, "inotify_init1() did not fail with bad flags.");
}

TEST(InotifyTest, InotifyInit) {
  int inotifyfd = inotify_init1(0);
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
}

TEST(InotifyTest, InotifyAddWatch) {
  int inotifyfd = inotify_init1(0);
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
  int ret_wd = inotify_add_watch(inotifyfd, "/path/test", IN_CREATE);
  ASSERT_GT(ret_wd, -1, "inotify_add_watch() failed.");
}

TEST(InotifyTest, InotifyAddWatchWithNullFilePath) {
  int inotifyfd = inotify_init1(0);
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
  int ret = inotify_add_watch(inotifyfd, 0, 0);
  ASSERT_EQ(ret, -1, "inotify_add_watch() did not fail with null filepath.");
}

TEST(InotifyTest, InotifyAddWatchWithZeroLengthFilePath) {
  int inotifyfd = inotify_init1(0);
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
  int ret = inotify_add_watch(inotifyfd, "\0", 0);
  ASSERT_EQ(ret, -1, "inotify_add_watch() did not fail with zero-length filepath.");
}

TEST(InotifyTest, InotifyAddWatchWithTooLongFilePath) {
  int inotifyfd = inotify_init1(0);
  std::string long_filepath(PATH_MAX + 1, 'x');
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
  int ret = inotify_add_watch(inotifyfd, long_filepath.c_str(), 0);
  ASSERT_EQ(ret, -1, "inotify_add_watch() did not fail with too long filepath.");
}

TEST(InotifyTest, InotifyRemoveWatch) {
  int inotifyfd = inotify_init1(0);
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
  int ret_wd = inotify_add_watch(inotifyfd, "/path/test", IN_CREATE);
  ASSERT_GT(ret_wd, -1, "inotify_add_watch() failed.");
  int ret = inotify_rm_watch(inotifyfd, ret_wd);
  ASSERT_EQ(ret, 0, "inotify_rm_watch() failed.");
}

TEST(InotifyTest, InotifyRemoveWatchWithInvalidInotifyDescriptor) {
  int inotifyfd = inotify_init1(0);
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
  int ret_wd = inotify_add_watch(inotifyfd, "/path/test", IN_CREATE);
  ASSERT_GT(ret_wd, -1, "inotify_add_watch() failed.");
  int ret = inotify_rm_watch(inotifyfd + 1, ret_wd);
  ASSERT_EQ(ret, -1, "inotify_rm_watch() did not fail with invalid inotify descriptor.");
}

TEST(InotifyTest, InotifyRemoveWatchWithInvalidWatchDescriptor) {
  int inotifyfd = inotify_init1(0);
  ASSERT_GT(inotifyfd, -1, "inotify_init1() failed");
  int ret_wd = inotify_add_watch(inotifyfd, "/path/test", IN_CREATE);
  ASSERT_GT(ret_wd, -1, "inotify_add_watch() failed.");
  int ret = inotify_rm_watch(inotifyfd, ret_wd + 1);
  ASSERT_EQ(ret, -1, "inotify_rm_watch() did not fail with invalid mask.");
}
