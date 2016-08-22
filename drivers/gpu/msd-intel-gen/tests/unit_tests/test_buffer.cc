// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_buffer.h"
#include "gtest/gtest.h"

TEST(MsdIntelBuffer, CreateAndDestroy)
{
    std::unique_ptr<MsdIntelBuffer> buffer;
    uint64_t size;

    buffer = MsdIntelBuffer::Create(size = 0);
    EXPECT_EQ(buffer, nullptr);

    buffer = MsdIntelBuffer::Create(size = 100);
    EXPECT_NE(buffer, nullptr);
    EXPECT_GE(buffer->platform_buffer()->size(), size);

    buffer = MsdIntelBuffer::Create(size = 10000);
    EXPECT_NE(buffer, nullptr);
    EXPECT_GE(buffer->platform_buffer()->size(), size);
}
