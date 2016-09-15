// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "magma_util/sleep.h"
#include "msd_intel_device.h"
#include "msd_intel_driver.h"
#include "gtest/gtest.h"

class TestEngineCommandStreamer {
public:
    static void ExecBatch(RenderEngineCommandStreamer* engine, MsdIntelContext* context,
                          gpu_addr_t batch_gpu_addr, uint32_t sequence_number)
    {
        EXPECT_TRUE(engine->StartBatchBuffer(context, batch_gpu_addr, ADDRESS_SPACE_GTT));
        EXPECT_TRUE(engine->WriteSequenceNumber(context, sequence_number));
        EXPECT_TRUE(engine->SubmitContext(context));
    }
};

class TestMsdIntelDevice {
public:
    TestMsdIntelDevice() { driver_ = MsdIntelDriver::Create(); }

    ~TestMsdIntelDevice() { MsdIntelDriver::Destroy(driver_); }

    void CreateAndDestroy()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        if (!platform_device) {
            printf("No platform device\n");
            return;
        }

        std::unique_ptr<MsdIntelDevice> device(
            driver_->CreateDevice(platform_device->GetDeviceHandle()));
        EXPECT_NE(device, nullptr);

        // TODO(MA-78) - replace sleeps everywhere in this file with proper wait
        magma::msleep(1000);

        // check that the render init batch succeeded.
        EXPECT_EQ(device->global_context()
                      ->hardware_status_page(RENDER_COMMAND_STREAMER)
                      ->read_sequence_number(),
                  0x1001u);

        // test register access
        uint32_t expected = 0xabcd1234;
        device->register_io()->Write32(0x4f100, expected);
        uint32_t value = device->register_io()->Read32(0x4f100);
        EXPECT_EQ(expected, value);
    }

    void Dump()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        if (!platform_device) {
            printf("No platform device\n");
            return;
        }

        std::unique_ptr<MsdIntelDevice> device(
            driver_->CreateDevice(platform_device->GetDeviceHandle()));
        EXPECT_NE(device, nullptr);

        magma::msleep(1000);

        MsdIntelDevice::DumpState dump_state;
        device->Dump(&dump_state);
        EXPECT_EQ(dump_state.render_cs.sequence_number,
                  device->global_context()
                      ->hardware_status_page(RENDER_COMMAND_STREAMER)
                      ->read_sequence_number());
        EXPECT_EQ(dump_state.render_cs.active_head_pointer,
                  device->render_engine_cs()->GetActiveHeadPointer());
        EXPECT_FALSE(dump_state.fault_present);

        uint32_t engine = 0;
        uint32_t src = 0xff;
        uint32_t type = 0x3;
        uint32_t valid = 0x1;
        device->DumpFault(&dump_state, (engine << 12) | (src << 3) | (type << 1) | valid);

        EXPECT_EQ(dump_state.fault_present, valid);
        EXPECT_EQ(dump_state.fault_engine, engine);
        EXPECT_EQ(dump_state.fault_src, src);
        EXPECT_EQ(dump_state.fault_type, type);

        std::string dump_string;
        device->DumpToString(dump_string);
        DLOG("%s", dump_string.c_str());
    }

    void BatchBuffer()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        if (!platform_device) {
            printf("No platform device\n");
            return;
        }

        MsdIntelDriver* driver = MsdIntelDriver::Create();
        ASSERT_NE(driver, nullptr);

        std::unique_ptr<MsdIntelDevice> device(
            driver->CreateDevice(platform_device->GetDeviceHandle()));
        EXPECT_NE(device, nullptr);

        DLOG("delay post init");
        magma::msleep(100);

        {
            std::string dump;
            device->DumpToString(dump);
            DLOG("dump: %s", dump.c_str());
        }

        auto target_buffer = MsdIntelBuffer::Create(PAGE_SIZE);
        ASSERT_NE(target_buffer, nullptr);

        void* target_cpu_addr;
        gpu_addr_t target_gpu_addr;

        EXPECT_TRUE(target_buffer->platform_buffer()->MapCpu(&target_cpu_addr));
        EXPECT_TRUE(target_buffer->MapGpu(device->gtt(), PAGE_SIZE));
        EXPECT_TRUE(target_buffer->GetGpuAddress(ADDRESS_SPACE_GTT, &target_gpu_addr));
        *reinterpret_cast<uint32_t*>(target_cpu_addr) = 0xdadabcbc;

        DLOG("target_cpu_addr %p", target_cpu_addr);
        DLOG("got target_gpu_addr 0x%llx", target_gpu_addr);

        auto batch_buffer = MsdIntelBuffer::Create(PAGE_SIZE);
        ASSERT_NE(batch_buffer, nullptr);

        void* batch_cpu_addr;
        gpu_addr_t batch_gpu_addr;
        EXPECT_TRUE(batch_buffer->platform_buffer()->MapCpu(&batch_cpu_addr));
        EXPECT_TRUE(batch_buffer->MapGpu(device->gtt(), PAGE_SIZE));
        EXPECT_TRUE(batch_buffer->GetGpuAddress(ADDRESS_SPACE_GTT, &batch_gpu_addr));

        DLOG("got batch_gpu_addr 0x%llx", batch_gpu_addr);

        static constexpr uint32_t kDwordCount = 4;
        static constexpr uint32_t kAddressSpaceGtt = 1 << 22;

        uint32_t expected_val = 0xdeadbeef;
        uint32_t* batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);

        // store dword
        *batch_ptr++ = (0x20 << 23) | (kDwordCount - 2) | kAddressSpaceGtt;
        *batch_ptr++ = magma::lower_32_bits(target_gpu_addr);
        *batch_ptr++ = magma::upper_32_bits(target_gpu_addr);
        *batch_ptr++ = expected_val;

        // batch end
        *batch_ptr++ = (0xA << 23);

        TestEngineCommandStreamer::ExecBatch(device->render_engine_cs(), device->global_context(),
                                             batch_gpu_addr, 0xabcd1234);

        magma::msleep(100);

        {
            std::string dump;
            device->DumpToString(dump);
            DLOG("dump: %s", dump.c_str());
        }

        DLOG("target_cpu_addr %p", target_cpu_addr);
        uint32_t target_val = *reinterpret_cast<uint32_t*>(target_cpu_addr);
        EXPECT_EQ(target_val, expected_val);
    }

private:
    MsdIntelDriver* driver_;
};

TEST(MsdIntelDevice, CreateAndDestroy)
{
    TestMsdIntelDevice test;
    test.CreateAndDestroy();
}

TEST(MsdIntelDevice, Dump)
{
    TestMsdIntelDevice test;
    test.Dump();
}

TEST(MsdIntelDevice, BatchBuffer)
{
    TestMsdIntelDevice test;
    test.BatchBuffer();
}
