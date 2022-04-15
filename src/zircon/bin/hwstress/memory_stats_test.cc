// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_stats.h"

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(GetMemoryStats, Simple) {
  auto maybe_stats = GetMemoryStats();
  ASSERT_TRUE(maybe_stats.is_ok());
  EXPECT_GT(maybe_stats.value().total_bytes(), 0u);
}

}  // namespace
}  // namespace hwstress
