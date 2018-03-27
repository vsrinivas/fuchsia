// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
};

TEST(PlatformHandle, Test) { TestPlatformHandle::Test(); }
