// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/thread_safe_queue.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media_audio {
namespace {

TEST(ThreadSafeQueueTest, PushPop) {
  ThreadSafeQueue<int> q;

  q.push(1);
  q.push(2);

  EXPECT_EQ(q.pop(), 1);
  EXPECT_EQ(q.pop(), 2);
  EXPECT_EQ(q.pop(), std::nullopt);

  q.push(3);
  EXPECT_EQ(q.pop(), 3);
  EXPECT_EQ(q.pop(), std::nullopt);
}

}  // namespace
}  // namespace media_audio
