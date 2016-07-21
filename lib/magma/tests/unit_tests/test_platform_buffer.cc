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
#include <platform_buffer.h>

static void TestPlatformBuffer(uint64_t size)
{
    printf("TestPlatformBuffer size 0x%llx\n", size);

    PlatformBuffer* buffer = PlatformBuffer::Create(size);
    if (size == 0) {
        EXPECT_EQ(buffer, nullptr);
        return;
    }

    EXPECT_NE(buffer, nullptr);
    EXPECT_GE(buffer->size(), size);

    void* virt_addr = nullptr;
    int ret = buffer->Map(&virt_addr);
    EXPECT_EQ(ret, 0);
    EXPECT_NE(virt_addr, nullptr);

    // write first word
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr)) = 0;
    // write last word
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) + buffer->size() - 4) = 0;

    ret = buffer->Unmap();
    EXPECT_EQ(ret, 0);

    delete buffer;
}

static void TestPlatformBufferPassing(PlatformBuffer* buf1, PlatformBuffer* buf2)
{
    printf("TestPlatformBufferPassing %p %p\n", buf1, buf2);

    bool again = !buf2;
    if (!buf1)
        buf1 = buf2;

    auto buf = PlatformBuffer::Create(buf1->token());

    EXPECT_EQ(buf1->size(), buf->size());
    EXPECT_EQ(buf1->handle(), buf->handle());

    void* virt_addr[]{};
    int ret = buf1->Map(&virt_addr[0]);
    EXPECT_EQ(ret, 0);
    ret = buf->Map(&virt_addr[1]);
    EXPECT_EQ(ret, 0);

    unsigned int some_offset = buf->size() / 2;
    int old_value =
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset);
    int check =
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
    EXPECT_EQ(old_value, check);

    int new_value = old_value + 1;
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset) =
        new_value;
    check = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
    EXPECT_EQ(new_value, check);

    ret = buf->Unmap();
    EXPECT_EQ(ret, 0);

    delete buf1;

    if (again)
        TestPlatformBufferPassing(nullptr, buf);
    else
        delete buf;
}

TEST(Magma, PlatformBuffer)
{
    TestPlatformBuffer(0);
    TestPlatformBuffer(1);
    TestPlatformBuffer(4095);
    TestPlatformBuffer(4096);
    TestPlatformBuffer(4097);
    TestPlatformBuffer(1024 * 1000 * 1000);
    TestPlatformBufferPassing(PlatformBuffer::Create(1), nullptr);
}
