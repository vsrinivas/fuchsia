// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/platform/platform_buffer.h"
#include "msd.h"
#include "gtest/gtest.h"

TEST(MsdBuffer, ImportAndDestroy)
{
    msd_platform_buffer* platform_buffer_token;

    auto platform_buf = magma::PlatformBuffer::Create(4096, &platform_buffer_token);
    ASSERT_NE(platform_buf, nullptr);
    ASSERT_EQ(platform_buf->GetRefCount(), 1u);

    auto msd_buffer = msd_buffer_import(platform_buffer_token);
    ASSERT_NE(msd_buffer, nullptr);
    EXPECT_EQ(platform_buf->GetRefCount(), 2u);

    msd_buffer_destroy(msd_buffer);
    EXPECT_EQ(platform_buf->GetRefCount(), 1u);
}