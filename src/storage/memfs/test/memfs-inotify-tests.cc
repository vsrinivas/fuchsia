// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include "src/lib/storage/vfs/cpp/inotify_test_base.h"

namespace {
class MemfsInotifyTest : public fs::InotifyTest {
 public:
  MemfsInotifyTest() {}
};

TEST_F(MemfsInotifyTest, InotifyCreateEvent) {
  // Initialize test directory
  MakeDir("base-dir");

  fbl::unique_fd inotify_fd;
  ASSERT_TRUE(inotify_fd = fbl::unique_fd(inotify_init1(0)), "%s", strerror(errno));

  // Add filter on base directory for notifying on file/directory create.
  int wd = inotify_add_watch(inotify_fd.get(), "/fshost-inotify-tmp/base-dir", IN_CREATE);
  ASSERT_GE(wd, 0);

  // Creating another directory on the same level shouldn't trigger IN_CREATE.
  MakeDir("irrelevant-dir");
  struct inotify_event event;
  // TODO: add checks on not triggering.

  // Try creating a directory inside base directory.
  MakeDir("base-dir/trigger-dir");
  ASSERT_EQ(read(inotify_fd.get(), &event, sizeof(event)), sizeof(event), "%s", strerror(errno));
  ASSERT_EQ(event.mask, IN_CREATE);
  ASSERT_EQ(event.wd, wd);

  // Try creating a file inside base directory.
  AddFile("base-dir/a.txt", 0);
  ASSERT_EQ(read(inotify_fd.get(), &event, sizeof(event)), sizeof(event), "%s", strerror(errno));
  ASSERT_EQ(event.mask, IN_CREATE);
  ASSERT_EQ(event.wd, wd);

  // TODO: testing link(2), symlink(2), bind(2) if supported.

  // remove filter
  ASSERT_EQ(inotify_rm_watch(inotify_fd.get(), wd), 0, "%s", strerror(errno));
}

}  // namespace
