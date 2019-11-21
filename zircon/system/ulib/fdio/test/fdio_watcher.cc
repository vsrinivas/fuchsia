// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/watcher.h>

#include <zxtest/zxtest.h>

TEST(WatcherTest, WatchInvalidDirFD) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_watch_directory(-1, nullptr, ZX_TIME_INFINITE, nullptr));
}
