// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include "gtest/gtest.h"
#include "platform_buffer.h"
#include "platform_handle.h"

class TestPlatformHandle {
 public:
  static void Test() {
    uint32_t mock_handle = 0x1001abba;
    auto platform_handle = magma::PlatformHandle::Create(mock_handle);
    ASSERT_TRUE(platform_handle);
    EXPECT_EQ(platform_handle->release(), mock_handle);
  }

  static void Count() {
    if (!magma::PlatformHandle::SupportsGetCount())
      GTEST_SKIP();
    auto buffer = magma::PlatformBuffer::Create(sysconf(_SC_PAGESIZE), "test");
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

  static void Duplicate() {
    auto buffer = magma::PlatformBuffer::Create(sysconf(_SC_PAGESIZE), "test");
    ASSERT_NE(buffer, nullptr);
    uint32_t raw_handle;
    EXPECT_TRUE(buffer->duplicate_handle(&raw_handle));
    uint32_t raw_handle2;
    EXPECT_TRUE(magma::PlatformHandle::duplicate_handle(raw_handle, &raw_handle2));
    EXPECT_NE(raw_handle, raw_handle2);

    auto handle1 = magma::PlatformHandle::Create(raw_handle);
    EXPECT_TRUE(handle1 != nullptr);
    auto handle2 = magma::PlatformHandle::Create(raw_handle2);
    EXPECT_TRUE(handle2 != nullptr);
  }
};

TEST(PlatformHandle, Test) { TestPlatformHandle::Test(); }

TEST(PlatformHandle, Count) { TestPlatformHandle::Count(); }

TEST(PlatformHandle, Duplicate) { TestPlatformHandle::Duplicate(); }
