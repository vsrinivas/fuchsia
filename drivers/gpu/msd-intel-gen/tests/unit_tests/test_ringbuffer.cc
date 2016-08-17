// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ringbuffer.h"
#include "gtest/gtest.h"

TEST(Ringbuffer, CreateAndDestroy)
{
    std::unique_ptr<Ringbuffer> ringbuffer(Ringbuffer::Create());
    ASSERT_NE(ringbuffer, nullptr);
    uint32_t expected = Ringbuffer::kRingbufferSize;
    EXPECT_EQ(ringbuffer->size(), expected);
}
