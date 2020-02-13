// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/sys/test/cpp/fidl.h>

#include <chrono>
#include <thread>

#include "gmock/gmock.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = ::llcpp::fuchsia::io;

TEST_F(NamespaceTest, HasCacheStorage) {
  ExpectExists("/cache");
  ExpectPathSupportsStrictRights("/cache", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE);
}

TEST_F(NamespaceTest, CanClearCacheStorage) {
  fuchsia::sys::test::CacheControlPtr cache;
  ConnectToService(cache.NewRequest());
  RunLoopUntilIdle();

  EXPECT_TRUE(files::WriteFile("/cache/test", "foobar", 7));

  std::vector<std::string> files;
  ASSERT_TRUE(files::ReadDirContents("/cache", &files));
  EXPECT_THAT(files, ::testing::UnorderedElementsAre(".", "test"));

  bool cache_cleared = false;
  cache->Clear([&] { cache_cleared = true; });
  RunLoopUntil([&] { return cache_cleared; });

  ASSERT_TRUE(files::ReadDirContents("/cache", &files));
  EXPECT_THAT(files, ::testing::UnorderedElementsAre("."));
}
