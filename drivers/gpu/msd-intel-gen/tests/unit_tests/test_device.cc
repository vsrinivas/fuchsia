// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "mock/mock_mmio.h"
#include "msd_intel_device.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestEngineCommandStreamer {
public:
    static bool ExecBatch(RenderEngineCommandStreamer* engine,
                          std::unique_ptr<MappedBatch> mapped_batch)
    {
        return engine->ExecBatch(std::move(mapped_batch));
    }
};

// These tests are unit testing the functionality of MsdIntelDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class TestMsdIntelDevice {
public:
    void CreateAndDestroy()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device =
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false);
        EXPECT_NE(device, nullptr);

        EXPECT_TRUE(device->WaitIdle());

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
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device =
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false);
        EXPECT_NE(device, nullptr);

        EXPECT_TRUE(device->WaitIdle());

        MsdIntelDevice::DumpState dump_state;
        device->Dump(&dump_state);
        EXPECT_EQ(dump_state.render_cs.sequence_number,
                  device->global_context()
                      ->hardware_status_page(RENDER_COMMAND_STREAMER)
                      ->read_sequence_number());
        EXPECT_EQ(dump_state.render_cs.active_head_pointer,
                  device->render_engine_cs()->GetActiveHeadPointer());
        EXPECT_FALSE(dump_state.fault_present);

        std::string dump_string;
        device->DumpToString(dump_string);
        DLOG("%s", dump_string.c_str());
    }

    void MockDump()
    {
        std::unique_ptr<RegisterIo> reg_io(new RegisterIo(MockMmio::Create(2 * 1024 * 1024)));

        reg_io->Write32(registers::FaultTlbReadData::kOffset0, 0xabcd1234);
        reg_io->Write32(registers::FaultTlbReadData::kOffset1, 0xf);

        MsdIntelDevice::DumpState dump_state;
        MsdIntelDevice::DumpFaultAddress(&dump_state, reg_io.get());

        EXPECT_EQ(dump_state.fault_gpu_address, 0xfabcd1234000ull);

        uint32_t engine = 0;
        uint32_t src = 0xff;
        uint32_t type = 0x3;
        uint32_t valid = 0x1;
        MsdIntelDevice::DumpFault(&dump_state, (engine << 12) | (src << 3) | (type << 1) | valid);

        EXPECT_EQ(dump_state.fault_present, valid);
        EXPECT_EQ(dump_state.fault_engine, engine);
        EXPECT_EQ(dump_state.fault_src, src);
        EXPECT_EQ(dump_state.fault_type, type);
    }

    void BatchBuffer(bool should_wrap_ringbuffer)
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device(
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false));
        EXPECT_NE(device, nullptr);

        EXPECT_TRUE(device->WaitIdle());

        auto mapping =
            AddressSpace::MapBufferGpu(device->gtt(), MsdIntelBuffer::Create(PAGE_SIZE), PAGE_SIZE);
        ASSERT_NE(mapping, nullptr);

        gpu_addr_t target_gpu_addr = mapping->gpu_addr();

        void* target_cpu_addr;
        EXPECT_TRUE(mapping->buffer()->platform_buffer()->MapCpu(&target_cpu_addr));

        auto batch_buffer = std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE));
        ASSERT_NE(batch_buffer, nullptr);

        void* batch_cpu_addr;
        EXPECT_TRUE(batch_buffer->platform_buffer()->MapCpu(&batch_cpu_addr));

        static constexpr uint32_t kDwordCount = 4;
        static constexpr uint32_t kAddressSpaceGtt = 1 << 22;

        uint32_t* batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);

        // store dword
        batch_ptr[0] = (0x20 << 23) | (kDwordCount - 2) | kAddressSpaceGtt;
        batch_ptr[1] = magma::lower_32_bits(target_gpu_addr);
        batch_ptr[2] = magma::upper_32_bits(target_gpu_addr);
        batch_ptr[3] = 0; // expected value

        // batch end
        batch_ptr[4] = (0xA << 23);

        bool ringbuffer_wrapped = false;

        // num_iterations updated after one iteration in case we're wrapping the ringbuffer
        uint32_t num_iterations = 1;

        for (uint32_t iteration = 0; iteration < num_iterations; iteration++) {
            auto batch_mapping = AddressSpace::MapBufferGpu(device->gtt(), batch_buffer, PAGE_SIZE);
            ASSERT_NE(batch_mapping, nullptr);

            // Initialize the target
            *reinterpret_cast<uint32_t*>(target_cpu_addr) = 0xdeadbeef;

            uint32_t expected_val = 0x8000000 + iteration;
            batch_ptr[3] = expected_val;

            auto ringbuffer =
                device->global_context()->get_ringbuffer(device->render_engine_cs()->id());

            uint32_t tail_start = ringbuffer->tail();

            EXPECT_TRUE(TestEngineCommandStreamer::ExecBatch(
                device->render_engine_cs(),
                std::unique_ptr<SimpleMappedBatch>(
                    new SimpleMappedBatch(device->global_context(), std::move(batch_mapping)))));

            EXPECT_TRUE(device->WaitIdle());

            EXPECT_EQ(ringbuffer->head(), ringbuffer->tail());

            uint32_t target_val = *reinterpret_cast<uint32_t*>(target_cpu_addr);
            EXPECT_EQ(target_val, expected_val);

            if (ringbuffer->tail() < tail_start) {
                DLOG("ringbuffer wrapped tail_start 0x%x tail 0x%x", tail_start,
                     ringbuffer->tail());
                ringbuffer_wrapped = true;
            }

            if (should_wrap_ringbuffer && num_iterations == 1)
                num_iterations =
                    (ringbuffer->size() - tail_start) / (ringbuffer->tail() - tail_start) + 10;

            // printf("completed iteration %d num_iterations %d target_val 0x%x\n", iteration,
            // num_iterations, target_val);
        }

        if (should_wrap_ringbuffer)
            EXPECT_TRUE(ringbuffer_wrapped);
    }

    void ProcessRequest()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device(
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false));
        ASSERT_NE(device, nullptr);

        class TestRequest : public DeviceRequest {
        public:
            TestRequest(std::shared_ptr<bool> processing_complete)
                : processing_complete_(processing_complete)
            {
            }

        protected:
            magma::Status Process(MsdIntelDevice* device) override
            {
                *processing_complete_ = true;
                return MAGMA_STATUS_OK;
            }

        private:
            std::shared_ptr<bool> processing_complete_;
        };

        auto processing_complete = std::make_shared<bool>(false);

        std::list<std::unique_ptr<DeviceRequest>> list;
        list.push_back(std::make_unique<TestRequest>(processing_complete));

        device->ProcessDeviceRequests(std::move(list));

        EXPECT_TRUE(processing_complete);
    }

    void MaxFreq()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device =
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false);
        EXPECT_NE(device, nullptr);

        constexpr uint32_t max_freq = 1050;
        uint32_t current_freq = device->GetCurrentFrequency();
        DLOG("current_freq %u max_freq %u", current_freq, max_freq);
        EXPECT_LE(current_freq, max_freq);

        device->RequestMaxFreq();

        uint32_t freq = device->GetCurrentFrequency();
        EXPECT_LE(freq, max_freq);

        EXPECT_GE(freq, current_freq);
    }

    void QuerySliceInfo()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device =
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false);
        EXPECT_NE(device, nullptr);

        uint32_t subslice_total = 0, eu_total = 0;
        device->QuerySliceInfo(&subslice_total, &eu_total);

        EXPECT_EQ(3u, subslice_total);
        // Expected values for eu_total are either 24 (for the Acer Switch
        // Alpha 12) or 23 (for the Intel NUC).
        if (eu_total != 24u)
            EXPECT_EQ(23u, eu_total);
    }
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

TEST(MsdIntelDevice, MockDump)
{
    TestMsdIntelDevice test;
    test.MockDump();
}

TEST(MsdIntelDevice, BatchBuffer)
{
    TestMsdIntelDevice test;
    test.BatchBuffer(false);
}

TEST(MsdIntelDevice, WrapRingbuffer)
{
    TestMsdIntelDevice test;
    test.BatchBuffer(true);
}

TEST(MsdIntelDevice, ProcessRequest)
{
    TestMsdIntelDevice test;
    test.ProcessRequest();
}

TEST(MsdIntelDevice, MaxFreq)
{
    TestMsdIntelDevice test;
    test.MaxFreq();
}

TEST(MsdIntelDevice, QuerySliceInfo)
{
    TestMsdIntelDevice test;
    test.QuerySliceInfo();
}
