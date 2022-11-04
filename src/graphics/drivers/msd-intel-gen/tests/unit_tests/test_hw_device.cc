// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magma_intel_gen_defs.h>

#include <gtest/gtest.h>

#include "device_request.h"
#include "helper/platform_device_helper.h"
#include "mock/mock_mmio.h"
#include "msd_intel_device.h"
#include "registers.h"

class TestEngineCommandStreamer {
 public:
  static bool ExecBatch(EngineCommandStreamer* engine, std::unique_ptr<MappedBatch> mapped_batch) {
    return engine->ExecBatch(std::move(mapped_batch));
  }
  static bool SubmitContext(EngineCommandStreamer* engine, MsdIntelContext* context,
                            uint32_t tail) {
    return engine->SubmitContext(context, tail);
  }
};

// These tests are unit testing the functionality of MsdIntelDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
constexpr bool kEnableDeviceThread = false;

class TestMsdIntelDevice : public testing::Test {
 public:
  void CreateAndDestroy() {
    for (uint32_t i = 0; i < 100; i++) {
      magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
      ASSERT_NE(platform_device, nullptr);

      std::unique_ptr<MsdIntelDevice> device =
          MsdIntelDevice::Create(platform_device->GetDeviceHandle(), kEnableDeviceThread);
      ASSERT_TRUE(device);

      ASSERT_TRUE(device->WaitIdleForTest());

      constexpr uint32_t kRenderCsDefaultSeqNo = 0x1000;
      ASSERT_EQ(device->render_engine_cs()->hardware_status_page()->read_sequence_number(),
                kRenderCsDefaultSeqNo);

      // test register access
      uint32_t expected = 0xabcd1234;
      device->register_io()->Write32(expected, 0x4f100);
      uint32_t value = device->register_io()->Read32(0x4f100);
      ASSERT_EQ(expected, value);

      EXPECT_TRUE(device->engines_have_context_isolation());

      if (DeviceId::is_gen12(device->device_id())) {
        EXPECT_EQ(19'200'000ul, device->timestamp_frequency());
      } else {
        EXPECT_EQ(12'000'000ul, device->timestamp_frequency());
      }
    }
  }

  class FormattedString {
   public:
    FormattedString(const char* fmt, ...) {
      va_list args;
      va_start(args, fmt);
      int size = std::vsnprintf(nullptr, 0, fmt, args);
      buf_ = std::vector<char>(size + 1);
      std::vsnprintf(buf_.data(), buf_.size(), fmt, args);
      va_end(args);
    }

    char* data() { return buf_.data(); }

   private:
    std::vector<char> buf_;
  };

  void Dump() {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device =
        MsdIntelDevice::Create(platform_device->GetDeviceHandle(), kEnableDeviceThread);
    EXPECT_NE(device, nullptr);

    EXPECT_TRUE(device->WaitIdleForTest());

    MsdIntelDevice::DumpState dump_state;
    device->Dump(&dump_state);

    EXPECT_EQ(dump_state.render_cs.sequence_number,
              device->render_engine_cs()->hardware_status_page()->read_sequence_number());
    EXPECT_EQ(dump_state.render_cs.active_head_pointer,
              device->render_engine_cs()->GetActiveHeadPointer());

    EXPECT_EQ(dump_state.video_cs.sequence_number,
              device->video_command_streamer()->hardware_status_page()->read_sequence_number());
    EXPECT_EQ(dump_state.video_cs.active_head_pointer,
              device->video_command_streamer()->GetActiveHeadPointer());

    EXPECT_FALSE(dump_state.fault_present);
    EXPECT_TRUE(dump_state.render_cs.inflight_batches.empty());

    dump_state.fault_present = true;
    dump_state.fault_engine = 0;
    dump_state.fault_src = 3;
    dump_state.fault_type = 10;
    dump_state.fault_gpu_address = 0xaabbccdd11223344;
    dump_state.global = 1;

    std::vector<std::string> dump_string;
    device->FormatDump(dump_state, dump_string);

    bool foundit = false;
    for (auto& str : dump_string) {
      if (strstr(str.c_str(),
                 FormattedString("sequence_number 0x%x", dump_state.render_cs.sequence_number)
                     .data())) {
        foundit = true;
        break;
      }
    }
    EXPECT_TRUE(foundit);

    foundit = false;
    for (auto& str : dump_string) {
      if (strstr(str.c_str(), FormattedString("active head pointer: 0x%llx",
                                              dump_state.render_cs.active_head_pointer)
                                  .data())) {
        foundit = true;
        break;
      }
    }
    EXPECT_TRUE(foundit);

    foundit = false;
    for (auto& str : dump_string) {
      if (strstr(
              str.c_str(),
              FormattedString("engine 0x%x src 0x%x type 0x%x gpu_address 0x%lx global %d",
                              dump_state.fault_engine, dump_state.fault_src, dump_state.fault_type,
                              dump_state.fault_gpu_address, dump_state.global)
                  .data())) {
        foundit = true;
        break;
      }
    }
    EXPECT_TRUE(foundit);
  }

  void MockDump() {
    auto reg_io = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(2 * 1024 * 1024));

    reg_io->Write32(0xabcd1234, registers::FaultTlbReadData::kOffset0);
    reg_io->Write32(0x1f, registers::FaultTlbReadData::kOffset1);

    MsdIntelDevice::DumpState dump_state;
    MsdIntelDevice::DumpFaultAddress(&dump_state, reg_io.get());
    EXPECT_EQ(0xfabcd1234000ull, dump_state.fault_gpu_address);
    EXPECT_TRUE(dump_state.global);

    reg_io->Write32(0xf, registers::FaultTlbReadData::kOffset1);
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

  static constexpr uint32_t load_data_immediate_header(uint32_t dword_count) {
    return (0x22 << 23) | (dword_count - 2);
  }
  static constexpr uint32_t store_data_immediate_header(uint32_t dword_count) {
    constexpr uint32_t kAddressSpaceGttBit = 1 << 22;
    return (0x20 << 23) | (dword_count - 2) | kAddressSpaceGttBit;
  }
  static constexpr uint32_t end_of_batch_header() { return (0xA << 23); }
  static EngineCommandStreamer* get_command_streamer(MsdIntelDevice* device,
                                                     EngineCommandStreamerId id) {
    switch (id) {
      case RENDER_COMMAND_STREAMER:
        return device->render_engine_cs();
      case VIDEO_COMMAND_STREAMER:
        return device->video_command_streamer();
      default:
        return nullptr;
    }
  }

  void BatchBuffer(bool should_wrap_ringbuffer, EngineCommandStreamerId id) {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device(
        MsdIntelDevice::Create(platform_device->GetDeviceHandle(), kEnableDeviceThread));
    ASSERT_NE(device, nullptr);

    EXPECT_TRUE(device->WaitIdleForTest());

    auto command_streamer = get_command_streamer(device.get(), id);
    ASSERT_TRUE(command_streamer);

    if (!device->global_context()->IsInitializedForEngine(id)) {
      ASSERT_TRUE(device->InitContextForEngine(device->global_context().get(), command_streamer));
    }

    bool ringbuffer_wrapped = false;

    // num_iterations updated after one iteration in case we're wrapping the ringbuffer
    uint32_t num_iterations = 1;

    for (uint32_t iteration = 0; iteration < num_iterations; iteration++) {
      auto dst_mapping =
          AddressSpace::MapBufferGpu(device->gtt(), MsdIntelBuffer::Create(PAGE_SIZE, "dst"));
      ASSERT_TRUE(dst_mapping);

      void* dst_cpu_addr;
      ASSERT_TRUE(dst_mapping->buffer()->platform_buffer()->MapCpu(&dst_cpu_addr));

      auto batch_buffer =
          std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "batchbuffer"));
      ASSERT_TRUE(batch_buffer);

      void* batch_cpu_addr;
      ASSERT_TRUE(batch_buffer->platform_buffer()->MapCpu(&batch_cpu_addr));
      uint32_t* batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);

      auto batch_mapping = AddressSpace::MapBufferGpu(device->gtt(), batch_buffer);
      ASSERT_TRUE(batch_mapping);

      uint32_t expected_val = 0x8000000 + iteration;
      uint64_t offset =
          (iteration * sizeof(uint32_t)) % dst_mapping->buffer()->platform_buffer()->size();

      // General purpose register 0
      const uint32_t kScratchRegOffset = command_streamer->mmio_base() + 0x600;

      uint32_t i = 0;
      batch_ptr[i++] = load_data_immediate_header(3 /*dword_count*/);
      batch_ptr[i++] = kScratchRegOffset;
      batch_ptr[i++] = expected_val;

      batch_ptr[i++] = store_data_immediate_header(4 /*dword_count*/);
      batch_ptr[i++] = magma::lower_32_bits(dst_mapping->gpu_addr() + offset);
      batch_ptr[i++] = magma::upper_32_bits(dst_mapping->gpu_addr() + offset);
      batch_ptr[i++] = expected_val;

      batch_ptr[i++] = end_of_batch_header();

      auto ringbuffer = device->global_context()->get_ringbuffer(command_streamer->id());
      ASSERT_TRUE(ringbuffer);

      uint32_t tail_start = ringbuffer->tail();

      auto forcewake = command_streamer->ForceWakeRequest();

      // Initialize the target
      reinterpret_cast<uint32_t*>(dst_cpu_addr)[offset / sizeof(uint32_t)] = 0xdeadbeef;
      device->register_io()->Write32(0xdeadbeef, kScratchRegOffset);

      ASSERT_TRUE(TestEngineCommandStreamer::ExecBatch(
          command_streamer, std::unique_ptr<SimpleMappedBatch>(new SimpleMappedBatch(
                                device->global_context(), std::move(batch_mapping)))));

      ASSERT_TRUE(device->WaitIdleForTest());

      ASSERT_EQ(ringbuffer->head(), ringbuffer->tail());

      ASSERT_EQ(expected_val, device->register_io()->Read32(kScratchRegOffset))
          << " iteration " << iteration;

      uint32_t target_val = reinterpret_cast<uint32_t*>(dst_cpu_addr)[offset / sizeof(uint32_t)];
      ASSERT_EQ(target_val, expected_val);

      if (ringbuffer->tail() < tail_start) {
        DLOG("ringbuffer wrapped tail_start 0x%x tail 0x%x", tail_start, ringbuffer->tail());
        ringbuffer_wrapped = true;
      }

      if (should_wrap_ringbuffer && num_iterations == 1)
        num_iterations = (ringbuffer->size() - tail_start) / (ringbuffer->tail() - tail_start) + 10;
    }

    if (should_wrap_ringbuffer)
      EXPECT_TRUE(ringbuffer_wrapped);

    DLOG("Finished, num_iterations %u", num_iterations);
  }

  void RegisterWrite(EngineCommandStreamerId id) {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device(new MsdIntelDevice());
    ASSERT_NE(device, nullptr);

    ASSERT_TRUE(device->Init(platform_device->GetDeviceHandle()));

    auto command_streamer = get_command_streamer(device.get(), id);
    ASSERT_TRUE(command_streamer);

    if (!device->global_context()->IsInitializedForEngine(id)) {
      ASSERT_TRUE(device->InitContextForEngine(device->global_context().get(), command_streamer));
    }

    auto ringbuffer = device->global_context()->get_ringbuffer(command_streamer->id());
    ASSERT_TRUE(ringbuffer);

    // General purpose register 0
    auto forcewake = command_streamer->ForceWakeRequest();

    const uint32_t kScratchRegOffset = command_streamer->mmio_base() + 0x600;
    device->register_io()->Write32(0xdeadbeef, kScratchRegOffset);

    ringbuffer->Write32(0);  // precede load with noop
    static constexpr uint32_t kExpectedVal = 0x8000000;
    constexpr uint32_t kCommandType = 0x22;     // store to mmio
    constexpr uint32_t kEncodedLength = 3 - 2;  // per instruction encoding
    ringbuffer->Write32((kCommandType << 23) | kEncodedLength);
    ringbuffer->Write32(kScratchRegOffset);
    ringbuffer->Write32(kExpectedVal);

    TestEngineCommandStreamer::SubmitContext(command_streamer, device->global_context().get(),
                                             ringbuffer->tail());

    auto start = std::chrono::steady_clock::now();
    // Check the register change first, the active head may fluctuate while the
    // context is loading.
    while (kExpectedVal != device->register_io()->Read32(kScratchRegOffset)) {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                start)
              .count() > 100) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(ringbuffer->tail(), command_streamer->GetRingbufferHeadPointer());
    EXPECT_EQ(kExpectedVal, device->register_io()->Read32(kScratchRegOffset));
  }

  void ProcessRequest() {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device(
        MsdIntelDevice::Create(platform_device->GetDeviceHandle(), kEnableDeviceThread));
    ASSERT_NE(device, nullptr);

    class TestRequest : public MsdIntelDevice::DeviceRequest {
     public:
      TestRequest(std::shared_ptr<bool> processing_complete)
          : processing_complete_(processing_complete) {}

     protected:
      magma::Status Process(MsdIntelDevice* device) override {
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

  void MaxFreq() {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device =
        MsdIntelDevice::Create(platform_device->GetDeviceHandle(), kEnableDeviceThread);
    EXPECT_NE(device, nullptr);

    constexpr uint32_t kMaxFreq = 1100;
    uint32_t current_freq = device->GetCurrentFrequency();
    DLOG("current_freq %u max_freq %u", current_freq, kMaxFreq);
    EXPECT_LE(current_freq, kMaxFreq);

    device->RequestMaxFreq();

    uint32_t freq = device->GetCurrentFrequency();
    EXPECT_LE(freq, kMaxFreq);

    EXPECT_GE(freq, current_freq);
  }

  void QuerySliceInfo() {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_TRUE(platform_device);

    std::unique_ptr<MsdIntelDevice> device =
        MsdIntelDevice::Create(platform_device->GetDeviceHandle(), kEnableDeviceThread);
    ASSERT_TRUE(device);

    if (DeviceId::is_gen12(device->device_id())) {
      EXPECT_EQ(5u, device->subslice_total());  // NUC11BNH
      EXPECT_EQ(80u, device->eu_total());

      auto [topology, mask_data] = device->GetTopology();
      ASSERT_TRUE(topology);

      EXPECT_EQ(topology->max_slice_count, 1u);
      EXPECT_EQ(topology->max_subslice_count, 6u);
      EXPECT_EQ(topology->max_eu_count, 16u);
      EXPECT_EQ(topology->data_byte_count, 1u + 1u + 5u * 2);

      EXPECT_EQ(mask_data[0], 1);     // slice enable mask
      EXPECT_EQ(mask_data[1], 0x1F);  // subslice enable mask

      for (int i = 0; i < 5; i++) {
        uint16_t eu_enable_mask;
        memcpy(&eu_enable_mask, &mask_data[2 + i * 2], sizeof(uint16_t));
        EXPECT_EQ(eu_enable_mask, 0xFFFF) << "index " << i;
      }

    } else {
      EXPECT_EQ(3u, device->subslice_total());
      if (device->eu_total() != 24)
        EXPECT_EQ(23u, device->eu_total());

      auto [topology, mask_data] = device->GetTopology();
      ASSERT_TRUE(topology);

      EXPECT_EQ(topology->max_slice_count, 3u);
      EXPECT_EQ(topology->max_subslice_count, 4u);
      EXPECT_EQ(topology->max_eu_count, 8u);
      EXPECT_EQ(topology->data_byte_count, 1u + 1u + 3u);

      EXPECT_EQ(mask_data[0], 0x1);  // slice enable mask
      EXPECT_EQ(mask_data[1], 0x7);  // subslice enable mask

      EXPECT_EQ(mask_data[2], 0xFF);  // subslice 0 EU mask
      if (mask_data[3] != 0xFF)
        EXPECT_EQ(mask_data[3], 0xFD);  // subslice 1 EU mask
      EXPECT_EQ(mask_data[4], 0xFF);    // subslice 2 EU mask
    }
  }

  void QueryTimestamp() {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device =
        MsdIntelDevice::Create(platform_device->GetDeviceHandle(), kEnableDeviceThread);
    EXPECT_NE(device, nullptr);

    uint64_t last_timestamp = 0;

    for (uint32_t i = 0; i < 10; i++) {
      auto buffer = std::shared_ptr<magma::PlatformBuffer>(
          magma::PlatformBuffer::Create(magma::page_size(), "timestamp test"));
      ASSERT_TRUE(buffer);

      ASSERT_EQ(MAGMA_STATUS_OK, device->ProcessTimestampRequest(buffer).get());

      void* ptr;
      ASSERT_TRUE(buffer->MapCpu(&ptr));

      auto query = reinterpret_cast<magma_intel_gen_timestamp_query*>(ptr);

      constexpr uint64_t kMask = (1ull << 36) - 1;  // from spec hw timestamp is 36bits
      EXPECT_EQ(0u, query->device_timestamp & ~kMask);

      EXPECT_GT(query->device_timestamp, last_timestamp);
      last_timestamp = query->device_timestamp;

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  class FakeSemaphore : public magma::PlatformSemaphore {
   public:
    void set_local_id(uint64_t id) override {}

    uint64_t id() const override { return 1; }
    uint64_t global_id() const override { return 1; }

    bool duplicate_handle(uint32_t* handle_out) const override { return kEnableDeviceThread; }

    void Signal() override {
      signal_sem_->Signal();
      if (pass_thru_) {
        sem_->Signal();
      }
    }

    void Reset() override {}

    magma::Status WaitNoReset(uint64_t timeout_ms) override { return MAGMA_STATUS_UNIMPLEMENTED; }

    magma::Status Wait(uint64_t timeout_ms) override {
      wait_sem_->Signal();
      sem_->Wait();
      return wait_return_.load();
    }

    bool WaitAsync(magma::PlatformPort* port, uint64_t key) override { return false; }

    std::unique_ptr<magma::PlatformSemaphore> sem_ = magma::PlatformSemaphore::Create();
    std::unique_ptr<magma::PlatformSemaphore> signal_sem_ = magma::PlatformSemaphore::Create();
    std::unique_ptr<magma::PlatformSemaphore> wait_sem_ = magma::PlatformSemaphore::Create();
    std::atomic<int32_t> wait_return_ = MAGMA_STATUS_OK;
    bool pass_thru_ = false;
  };

  void HangcheckTimeout(bool spurious, EngineCommandStreamerId id) {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device = std::unique_ptr<MsdIntelDevice>(new MsdIntelDevice());

    ASSERT_TRUE(device->Init(platform_device->GetDeviceHandle()));

    EXPECT_EQ(device->suspected_gpu_hang_count_.load(), 0u);

    device->device_request_semaphore_ = std::make_unique<FakeSemaphore>();

    auto semaphore = static_cast<FakeSemaphore*>(device->device_request_semaphore_.get());

    ASSERT_TRUE(device->StartDeviceThread());

    // Wait for device thread to idle
    while (true) {
      // Wait for device thread to Wait on device request semaphore
      EXPECT_EQ(MAGMA_STATUS_OK, semaphore->wait_sem_->Wait(2000).get());
      // See if any other thread signals the device request semaphore
      magma::Status status = semaphore->signal_sem_->Wait(2000);
      if (status.get() == MAGMA_STATUS_TIMED_OUT)
        break;
      semaphore->sem_->Signal();
    }

    // Device thread is idle.  Pretend a batch was submitted some time ago.
    // When device wakes up it should hangcheck.
    {
      EngineCommandStreamer* engine = nullptr;
      switch (id) {
        case RENDER_COMMAND_STREAMER:
          engine = device->render_engine_cs();
          break;
        case VIDEO_COMMAND_STREAMER:
          engine = device->video_command_streamer();
          break;
      }
      ASSERT_TRUE(engine);
      uint32_t sequence_number = engine->progress()->last_submitted_sequence_number() + 1;
      engine->progress()->Submitted(sequence_number,
                                    std::chrono::steady_clock::now() - std::chrono::seconds(5));
    }

    if (spurious) {
      // If work is enqueued then we should not hangcheck
      device->EnqueueDeviceRequest(std::make_unique<DeviceRequest<MsdIntelDevice>>());
    }

    // Device thread will receive a timed out result
    semaphore->wait_return_ = MAGMA_STATUS_TIMED_OUT;
    semaphore->sem_->Signal();

    // Wait for the device thread to again Wait on device request semaphore
    EXPECT_EQ(MAGMA_STATUS_OK, semaphore->wait_sem_->Wait(2000).get());
    EXPECT_EQ(device->suspected_gpu_hang_count_.load(), spurious ? 0u : 1u);

    semaphore->pass_thru_ = true;
    semaphore->wait_return_ = MAGMA_STATUS_OK;
    semaphore->sem_->Signal();
  }
};

TEST_F(TestMsdIntelDevice, CreateAndDestroy) { TestMsdIntelDevice::CreateAndDestroy(); }

TEST_F(TestMsdIntelDevice, Dump) { TestMsdIntelDevice::Dump(); }

TEST_F(TestMsdIntelDevice, MockDump) { TestMsdIntelDevice::MockDump(); }

TEST_F(TestMsdIntelDevice, ProcessRequest) { TestMsdIntelDevice::ProcessRequest(); }

TEST_F(TestMsdIntelDevice, MaxFreq) { TestMsdIntelDevice::MaxFreq(); }

TEST_F(TestMsdIntelDevice, QuerySliceInfo) { TestMsdIntelDevice::QuerySliceInfo(); }

TEST_F(TestMsdIntelDevice, QueryTimestamp) { TestMsdIntelDevice::QueryTimestamp(); }

class TestMsdIntelDevice_RenderCommandStreamer : public TestMsdIntelDevice {};

TEST_F(TestMsdIntelDevice_RenderCommandStreamer, RegisterWrite) {
  TestMsdIntelDevice::RegisterWrite(RENDER_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_RenderCommandStreamer, BatchBuffer) {
  TestMsdIntelDevice::BatchBuffer(false, RENDER_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_RenderCommandStreamer, WrapRingbuffer) {
  TestMsdIntelDevice::BatchBuffer(true, RENDER_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_RenderCommandStreamer, HangcheckTimeout) {
  TestMsdIntelDevice::HangcheckTimeout(false, RENDER_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_RenderCommandStreamer, SpuriousHangcheckTimeout) {
  TestMsdIntelDevice::HangcheckTimeout(true, RENDER_COMMAND_STREAMER);
}

class TestMsdIntelDevice_VideoCommandStreamer : public TestMsdIntelDevice {};

TEST_F(TestMsdIntelDevice_VideoCommandStreamer, RegisterWrite) {
  TestMsdIntelDevice::RegisterWrite(VIDEO_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_VideoCommandStreamer, BatchBuffer) {
  TestMsdIntelDevice::BatchBuffer(false, VIDEO_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_VideoCommandStreamer, WrapRingbuffer) {
  TestMsdIntelDevice::BatchBuffer(true, VIDEO_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_VideoCommandStreamer, HangcheckTimeout) {
  TestMsdIntelDevice::HangcheckTimeout(false, VIDEO_COMMAND_STREAMER);
}

TEST_F(TestMsdIntelDevice_VideoCommandStreamer, SpuriousHangcheckTimeout) {
  TestMsdIntelDevice::HangcheckTimeout(true, VIDEO_COMMAND_STREAMER);
}
