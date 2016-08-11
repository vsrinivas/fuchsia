// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "magma_util/platform_buffer.h"
#include "msd.h"
#include "gtest/gtest.h"

TEST(MsdIntelGen, MsdBuffer)
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