// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdlib.h>

#include "bench_vk.h"

//
// Enumerate {FILL,COPY,NOOP} x {FENCE,QUEUE,TIMELINE}
//
namespace bench::vk {

//
//
//
#define ARRAY_LENGTH_MACRO(x_) (sizeof(x_) / sizeof(x_[0]))

#define BENCH_VK(tokens_) bench_vk(ARRAY_LENGTH_MACRO(tokens_), tokens_)

//
//
//
TEST(BenchVkFill, Fence)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "fill", "256", "fence", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkFill, Queue)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "fill", "256", "queue", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkFill, Timeline)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "fill", "256", "timeline", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkCopy, Fence)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "copy", "256", "fence", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkCopy, Queue)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "copy", "256", "queue", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkCopy, Timeline)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "copy", "256", "timeline", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkNoop, Fence)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "noop", "fence", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkNoop, Queue)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "noop", "queue", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//
TEST(BenchVkNoop, Timeline)
{
  char const * tokens[] = {

    "bench-vk", "quiet", "noop", "timeline", "repetitions", "1", "warmup", "1"
  };

  EXPECT_EQ(BENCH_VK(tokens), EXIT_SUCCESS);
}

//
//
//

}  // namespace bench::vk

//
//
//
