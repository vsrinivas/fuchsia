// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/inotify.h>
#include <sys/ioctl.h>

#include "src/lib/storage/vfs/cpp/inotify_test_base.h"

namespace {
class VfsInotifyTest : public fs::InotifyTest {
 public:
  VfsInotifyTest() {}
};

// Tests basic open/close events for inotify.
TEST_F(VfsInotifyTest, BasicOpenClose) {
  // Initialize test directory
  MakeDir("a");
  AddFile("a/a.txt", 13);

  fbl::unique_fd inotify_fd;
  ASSERT_TRUE(inotify_fd = fbl::unique_fd(inotify_init1(0)), "%s", strerror(errno));

  // Add filter on file for notifying on file open and file close.
  int wd = inotify_add_watch(inotify_fd.get(), "/fshost-inotify-tmp/a/a.txt", IN_OPEN | IN_CLOSE);
  ASSERT_GE(wd, 0);

  // Check that no event is available
  unsigned int available_bytes;
  ASSERT_EQ(ioctl(inotify_fd.get(), FIONREAD, &available_bytes), 0, "%s", strerror(errno));
  ASSERT_EQ(0, available_bytes);

  // Try to open the file, with the filter.
  int fd = open("/fshost-inotify-tmp/a/a.txt", O_RDWR);
  ASSERT_GE(fd, 0);

  struct inotify_event event;

  // Retrieve the amount of data available to read.
  ASSERT_EQ(ioctl(inotify_fd.get(), FIONREAD, &available_bytes), 0, "%s", strerror(errno));
  ASSERT_GE(available_bytes, sizeof(event));

  // Read the open event.
  ASSERT_EQ(read(inotify_fd.get(), &event, sizeof(event)), sizeof(event), "%s", strerror(errno));
  ASSERT_EQ(event.mask, IN_OPEN);
  ASSERT_EQ(event.wd, wd);

  // Check that no more event is available
  ASSERT_EQ(ioctl(inotify_fd.get(), FIONREAD, &available_bytes), 0, "%s", strerror(errno));
  ASSERT_EQ(0, available_bytes);

  // Close the file
  ASSERT_EQ(close(fd), 0);

  // Read the close event.
  ASSERT_EQ(read(inotify_fd.get(), &event, sizeof(event)), sizeof(event), "%s", strerror(errno));
  ASSERT_EQ(event.mask, IN_CLOSE, "%s", "Returned inotify event is incorrect.");
  ASSERT_EQ(event.wd, wd, "%s", "Returned inotify watch descriptor is incorrect.");

  // remove filter
  ASSERT_EQ(inotify_rm_watch(inotify_fd.get(), wd), 0, "%s", strerror(errno));
}

}  // namespace
