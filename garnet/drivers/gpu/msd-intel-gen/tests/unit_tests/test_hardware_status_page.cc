// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_status_page.h"
#include "gtest/gtest.h"

using unique_ptr_void_free = std::unique_ptr<void, decltype(&free)>;

class TestHardwareStatusPage : public HardwareStatusPage::Owner {
public:
    TestHardwareStatusPage() : cpu_addr_(malloc(PAGE_SIZE), &free) {}

    void ReadWrite()
    {
        auto status_page = std::unique_ptr<HardwareStatusPage>(new HardwareStatusPage(this, id_));

        EXPECT_EQ(status_page->gpu_addr(), gpu_addr_);

        uint32_t val = 0xabcd1234;
        status_page->write_sequence_number(val);
        EXPECT_EQ(status_page->read_sequence_number(), val);

        status_page->write_sequence_number(val + 1);
        EXPECT_EQ(status_page->read_sequence_number(), val + 1);
    }

private:
    void* hardware_status_page_cpu_addr(EngineCommandStreamerId id) override
    {
        EXPECT_EQ(id, id_);
        return cpu_addr_.get();
    }

    gpu_addr_t hardware_status_page_gpu_addr(EngineCommandStreamerId id) override
    {
        EXPECT_EQ(id, id_);
        return gpu_addr_;
    }

    unique_ptr_void_free cpu_addr_;
    EngineCommandStreamerId id_ = RENDER_COMMAND_STREAMER;
    gpu_addr_t gpu_addr_ = 0x10000;
};

TEST(HardwareStatusPage, ReadWrite)
{
    TestHardwareStatusPage test;
    test.ReadWrite();
}
