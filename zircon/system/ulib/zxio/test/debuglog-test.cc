// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/zxio.h>

#include <array>
#include <thread>

#include <zxtest/zxtest.h>

TEST(DebugLogTest, TheadSafety) {
  // Sets up multiple thread and write to debuglog, ASAN or TSAN builders will
  // be responsible for catching bugs.
  constexpr size_t kNumThreads = 256;

  zx::debuglog handle;
  ASSERT_EQ(ZX_OK, zx::debuglog::create(zx::resource(), 0, &handle));

  auto storage = std::make_unique<zxio_storage_t>();
  ASSERT_EQ(ZX_OK, zxio_debuglog_init(storage.get(), std::move(handle)));
  zxio_t* logger = &storage->io;
  ASSERT_NE(logger, nullptr);

  std::array<std::thread, kNumThreads> threads;

  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i] = std::thread([i, logger]() {
      std::string log_str = "output from " + std::to_string(i);
      size_t actual;
      ASSERT_OK(zxio_write(logger, log_str.c_str(), log_str.size(), 0, &actual));
      ASSERT_EQ(actual, log_str.size());
    });
  }

  for (auto t = threads.rbegin(); t != threads.rend(); ++t) {
    t->join();
  }
}
