// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_buffer.h"
#include "platform_handle.h"
#include "gtest/gtest.h"

class TestPlatformHandle {
public:
    static void Test()
    {
        uint32_t mock_handle = 0xabba1001;
        auto platform_handle = magma::PlatformHandle::Create(mock_handle);
        ASSERT_TRUE(platform_handle);
        EXPECT_EQ(platform_handle->release(), mock_handle);
    }

    static void Count()
    {
        auto buffer = magma::PlatformBuffer::Create(PAGE_SIZE, "test");
        ASSERT_NE(buffer, nullptr);
        uint32_t raw_handle;
        EXPECT_TRUE(buffer->duplicate_handle(&raw_handle));
        auto handle = magma::PlatformHandle::Create(raw_handle);
        uint32_t count;
        EXPECT_TRUE(handle->GetCount(&count));
        EXPECT_EQ(2u, count);
        buffer.reset();
        EXPECT_TRUE(handle->GetCount(&count));
        EXPECT_EQ(1u, count);
    }
};

TEST(PlatformHandle, Test) { TestPlatformHandle::Test(); }

TEST(PlatformHandle, Count) { TestPlatformHandle::Count(); }
