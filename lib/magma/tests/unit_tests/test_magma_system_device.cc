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

#include "gtest/gtest.h"
#include <magma_system_device.h>

TEST(Magma, MagmaSystemDevice_BufferManagement)
{
    auto dev = MagmaSystemDevice(nullptr);

    uint64_t test_size = 4096;

    // allocating a zero size buffer should fail
    EXPECT_EQ(dev.AllocateBuffer(0), nullptr);

    auto buf = dev.AllocateBuffer(test_size);
    // assert because if this fails the rest of this is gonna be bogus anyway
    ASSERT_NE(buf, nullptr);
    EXPECT_GE(buf->size(), test_size);

    auto handle = buf->handle();
    EXPECT_EQ(handle, buf->platform_buffer()->handle());

    // should be able to get the buffer by handle
    auto get_buf = dev.LookupBuffer(handle);
    EXPECT_NE(get_buf, nullptr);
    EXPECT_EQ(get_buf, buf); // they are shared ptrs after all

    // freeing the allocated buffer should work
    EXPECT_TRUE(dev.FreeBuffer(handle));

    // should no longer be able to get it from the map
    EXPECT_EQ(dev.LookupBuffer(handle), nullptr);

    // should not be able to double free it
    EXPECT_FALSE(dev.FreeBuffer(handle));
}