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
    static bool SubmitContext(RenderEngineCommandStreamer* engine, MsdIntelContext* context,
                              uint32_t tail)
    {
        return engine->SubmitContext(context, tail);
    }
};

// These tests are unit testing the functionality of MsdIntelDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class TestMsdIntelDevice {
public:
    void CreateAndDestroy()
    {
        magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
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

    class FormattedString {
    public:
        FormattedString(const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            printf(": ");
            int size = std::vsnprintf(nullptr, 0, fmt, args);
            buf_ = std::vector<char>(size + 1);
            std::vsnprintf(buf_.data(), buf_.size(), fmt, args);
            va_end(args);
        }

        char* data() { return buf_.data(); }

    private:
        std::vector<char> buf_;
    };

    void Dump()
    {
        magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
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
        EXPECT_TRUE(dump_state.render_cs.inflight_batches.empty());

        dump_state.fault_present = true;
        dump_state.fault_engine = 0;
        dump_state.fault_src = 3;
        dump_state.fault_type = 10;
        dump_state.fault_gpu_address = 0xaabbccdd11223344;
        dump_state.global = 1;

        std::string dump_string;
        device->FormatDump(dump_state, dump_string);
        EXPECT_NE(nullptr,
                  strstr(dump_string.c_str(), FormattedString("sequence_number 0x%x",
                                                              dump_state.render_cs.sequence_number)
                                                  .data()));
        EXPECT_NE(nullptr, strstr(dump_string.c_str(),
                                  FormattedString("active head pointer: 0x%llx",
                                                  dump_state.render_cs.active_head_pointer)
                                      .data()));
        EXPECT_NE(nullptr,
                  strstr(dump_string.c_str(),
                         FormattedString(
                             "engine 0x%x src 0x%x type 0x%x gpu_address 0x%lx global %d",
                             dump_state.fault_engine, dump_state.fault_src, dump_state.fault_type,
                             dump_state.fault_gpu_address, dump_state.global)
                             .data()));
    }

    void MockDump()
    {
        auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(2 * 1024 * 1024));

        reg_io->Write32(registers::FaultTlbReadData::kOffset0, 0xabcd1234);
        reg_io->Write32(registers::FaultTlbReadData::kOffset1, 0x1f);

        MsdIntelDevice::DumpState dump_state;
        MsdIntelDevice::DumpFaultAddress(&dump_state, reg_io.get());
        EXPECT_EQ(0xfabcd1234000ull, dump_state.fault_gpu_address);
        EXPECT_TRUE(dump_state.global);

        reg_io->Write32(registers::FaultTlbReadData::kOffset1, 0xf);
        MsdIntelDevice::DumpFaultAddress(&dump_state, reg_io.get());
        EXPECT_EQ(0xfabcd1234000ull, dump_state.fault_gpu_address);
        EXPECT_FALSE(dump_state.global);

        uint32_t engine = 0;
        uint32_t src = 0xff;
        uint32_t type = 0x3;
        uint32_t valid = 0x1;
        MsdIntelDevice::DumpFault(&dump_state, (engine << 12) | (src << 3) | (type << 1) | valid);

        EXPECT_EQ(dump_state.fault_present, valid);
        EXPECT_EQ(dump_state.fault_engine, engine);
        EXPECT_EQ(dump_state.fault_src, src);
        EXPECT_EQ(dump_state.fault_type, type);
        EXPECT_TRUE(dump_state.render_cs.inflight_batches.empty());
    }

    void BatchBuffer(bool should_wrap_ringbuffer)
    {
        magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device(
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false));
        ASSERT_NE(device, nullptr);

        EXPECT_TRUE(device->WaitIdle());

        bool ringbuffer_wrapped = false;

        // num_iterations updated after one iteration in case we're wrapping the ringbuffer
        uint32_t num_iterations = 1;

        for (uint32_t iteration = 0; iteration < num_iterations; iteration++) {
            auto dst_mapping =
                AddressSpace::MapBufferGpu(device->gtt(), MsdIntelBuffer::Create(PAGE_SIZE, "dst"));
            ASSERT_NE(dst_mapping, nullptr);

            void* dst_cpu_addr;
            EXPECT_TRUE(dst_mapping->buffer()->platform_buffer()->MapCpu(&dst_cpu_addr));

            auto batch_buffer =
                std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "batchbuffer"));
            ASSERT_NE(batch_buffer, nullptr);

            void* batch_cpu_addr;
            ASSERT_TRUE(batch_buffer->platform_buffer()->MapCpu(&batch_cpu_addr));
            uint32_t* batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);

            auto batch_mapping = AddressSpace::MapBufferGpu(device->gtt(), batch_buffer);
            ASSERT_NE(batch_mapping, nullptr);

            uint32_t expected_val = 0x8000000 + iteration;
            uint64_t offset =
                (iteration * sizeof(uint32_t)) % dst_mapping->buffer()->platform_buffer()->size();

            static constexpr uint32_t kScratchRegOffset = 0x02600;

            uint32_t i = 0;
            batch_ptr[i++] = (0x22 << 23) | (3 - 2); // store to mmio
            batch_ptr[i++] = kScratchRegOffset;
            batch_ptr[i++] = expected_val;

            static constexpr uint32_t kDwordCount = 4;
            static constexpr uint32_t kAddressSpaceGtt = 1 << 22;

            batch_ptr[i++] = (0x20 << 23) | (kDwordCount - 2) | kAddressSpaceGtt; // store dword
            batch_ptr[i++] = magma::lower_32_bits(dst_mapping->gpu_addr() + offset);
            batch_ptr[i++] = magma::upper_32_bits(dst_mapping->gpu_addr() + offset);
            batch_ptr[i++] = expected_val;
            batch_ptr[i++] = (0xA << 23); // batch end

            auto ringbuffer =
                device->global_context()->get_ringbuffer(device->render_engine_cs()->id());

            uint32_t tail_start = ringbuffer->tail();

            // Initialize the target
            reinterpret_cast<uint32_t*>(dst_cpu_addr)[offset / sizeof(uint32_t)] = 0xdeadbeef;
            device->register_io()->Write32(kScratchRegOffset, 0xdeadbeef);

            EXPECT_TRUE(TestEngineCommandStreamer::ExecBatch(
                device->render_engine_cs(),
                std::unique_ptr<SimpleMappedBatch>(
                    new SimpleMappedBatch(device->global_context(), std::move(batch_mapping)))));

            EXPECT_TRUE(device->WaitIdle());

            EXPECT_EQ(ringbuffer->head(), ringbuffer->tail());

            EXPECT_EQ(expected_val, device->register_io()->Read32(kScratchRegOffset));

            uint32_t target_val =
                reinterpret_cast<uint32_t*>(dst_cpu_addr)[offset / sizeof(uint32_t)];
            EXPECT_EQ(target_val, expected_val);

            if (ringbuffer->tail() < tail_start) {
                DLOG("ringbuffer wrapped tail_start 0x%x tail 0x%x", tail_start,
                     ringbuffer->tail());
                ringbuffer_wrapped = true;
            }

            if (should_wrap_ringbuffer && num_iterations == 1)
                num_iterations =
                    (ringbuffer->size() - tail_start) / (ringbuffer->tail() - tail_start) + 10;
        }

        if (should_wrap_ringbuffer)
            EXPECT_TRUE(ringbuffer_wrapped);

        DLOG("Finished, num_iterations %u", num_iterations);
    }

    void RegisterWrite()
    {
        magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device(new MsdIntelDevice());
        ASSERT_NE(device, nullptr);

        ASSERT_TRUE(device->BaseInit(platform_device->GetDeviceHandle()));

        auto ringbuffer =
            device->global_context()->get_ringbuffer(device->render_engine_cs()->id());

        static constexpr uint32_t kScratchRegOffset = 0x02600;
        device->register_io()->Write32(kScratchRegOffset, 0xdeadbeef);

        static constexpr uint32_t expected_val = 0x8000000;
        ringbuffer->write_tail((0x22 << 23) | (3 - 2)); // store to mmio
        ringbuffer->write_tail(kScratchRegOffset);
        ringbuffer->write_tail(expected_val);
        ringbuffer->write_tail(0);

        TestEngineCommandStreamer::SubmitContext(
            device->render_engine_cs(), device->global_context().get(), ringbuffer->tail());

        auto start = std::chrono::high_resolution_clock::now();
        while (device->render_engine_cs()->GetActiveHeadPointer() != ringbuffer->tail() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - start)
                       .count() < 100) {
            std::this_thread::yield();
        }

        EXPECT_EQ(ringbuffer->tail(), device->render_engine_cs()->GetActiveHeadPointer());
        EXPECT_EQ(expected_val, device->register_io()->Read32(kScratchRegOffset));
    }

    void ProcessRequest()
    {
        magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
        ASSERT_NE(platform_device, nullptr);

        std::unique_ptr<MsdIntelDevice> device(
            MsdIntelDevice::Create(platform_device->GetDeviceHandle(), false));
        ASSERT_NE(device, nullptr);

        class TestRequest : public MsdIntelDevice::DeviceRequest {
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

        auto request = std::make_unique<TestRequest>(processing_complete);
        request->ProcessAndReply(device.get());

        EXPECT_TRUE(processing_complete);
    }

    void MaxFreq()
    {
        magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
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
        magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
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

TEST(MsdIntelDevice, RegisterWrite)
{
    TestMsdIntelDevice test;
    test.RegisterWrite();
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
