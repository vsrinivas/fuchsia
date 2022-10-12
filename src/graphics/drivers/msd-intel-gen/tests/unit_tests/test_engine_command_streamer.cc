// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>
#include <mock/mock_mapped_batch.h>
#include <mock/mock_mmio.h>

#include "address_space.h"
#include "device_id.h"
#include "gtt.h"
#include "instructions.h"
#include "register_tracer.h"
#include "registers.h"
#include "render_command_streamer.h"
#include "render_init_batch.h"
#include "sequencer.h"
#include "video_command_streamer.h"

using AllocatingAddressSpace = FakeAllocatingAddressSpace<GpuMapping, AddressSpace>;

class TestContext {
 public:
  static MsdIntelBuffer* get_context_buffer(MsdIntelContext* context, EngineCommandStreamerId id) {
    return context->get_context_buffer(id);
  }
};

class TestRingbuffer {
 public:
  static uint32_t* vaddr(Ringbuffer* ringbuffer) { return ringbuffer->vaddr(); }
};

struct TestParam {
  EngineCommandStreamerId id;
  uint32_t device_id;
};

class TestEngineCommandStreamer : public EngineCommandStreamer::Owner,
                                  public Gtt::Owner,
                                  public testing::TestWithParam<TestParam> {
 public:
  static constexpr uint32_t kFirstSequenceNumber = 5;

  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    virtual ~AddressSpaceOwner() = default;
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  void SetUp() override {
    register_io_ = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(8 * 1024 * 1024));

    std::weak_ptr<MsdIntelConnection> connection;

    context_ =
        std::shared_ptr<MsdIntelContext>(new MsdIntelContext(Gtt::CreateShim(this), connection));

    address_space_owner_ = std::make_unique<AddressSpaceOwner>();
    address_space_ =
        std::make_shared<AllocatingAddressSpace>(address_space_owner_.get(), 0, PAGE_SIZE * 100);

    sequencer_ = std::unique_ptr<Sequencer>(new Sequencer(kFirstSequenceNumber));

    auto mapping = AddressSpace::MapBufferGpu(address_space_,
                                              MsdIntelBuffer::Create(PAGE_SIZE, "global hwsp"));
    ASSERT_TRUE(mapping);

    switch (id()) {
      case RENDER_COMMAND_STREAMER:
        engine_cs_ = std::make_unique<RenderEngineCommandStreamer>(this, std::move(mapping));
        break;
      case VIDEO_COMMAND_STREAMER:
        engine_cs_ = std::make_unique<VideoCommandStreamer>(this, std::move(mapping));
        break;
    }
    ASSERT_TRUE(engine_cs_);
  }

  EngineCommandStreamerId id() const { return GetParam().id; }

  uint32_t mmio_base() {
    switch (id()) {
      case RENDER_COMMAND_STREAMER:
        return 0x2000;
      case VIDEO_COMMAND_STREAMER:
        if (DeviceId::is_gen12(device_id())) {
          return 0x1C0000;
        } else {
          return 0x12000;
        }
    }
    return 0;
  }

  void InitContext() {
    auto buffer = TestContext::get_context_buffer(context_.get(), engine_cs_->id());
    EXPECT_EQ(buffer, nullptr);

    EXPECT_TRUE(engine_cs_->InitContext(context_.get()));

    buffer = TestContext::get_context_buffer(context_.get(), engine_cs_->id());
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->platform_buffer()->size(), engine_cs_->GetContextSize());

    auto ringbuffer = context_->get_ringbuffer(engine_cs_->id());
    ASSERT_NE(ringbuffer, nullptr);

    void* addr;
    EXPECT_TRUE(buffer->platform_buffer()->MapCpu(&addr));

    uint32_t* state = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(addr) + PAGE_SIZE);

    if (DeviceId::is_gen12(device_id())) {
      EXPECT_EQ(state[1], 0x11081019ul);
      EXPECT_EQ(state[2], mmio_base() + 0x244ul);
      switch (id()) {
        case RENDER_COMMAND_STREAMER:
          EXPECT_EQ(state[3], 0x00090009ul);
          break;
        case VIDEO_COMMAND_STREAMER:
          EXPECT_EQ(state[3], 0x00090009ul);
          break;
      }
      EXPECT_EQ(state[4], mmio_base() + 0x34ul);
      EXPECT_EQ(state[5], ringbuffer->head());
      EXPECT_EQ(state[6], mmio_base() + 0x30ul);
      EXPECT_EQ(state[7], 0u);
      EXPECT_EQ(state[8], mmio_base() + 0x38ul);
      // state[9] is not set until later
      EXPECT_EQ(state[0xA], mmio_base() + 0x3Cul);
      EXPECT_EQ(state[0xB], (31ul * PAGE_SIZE) | 1);
      EXPECT_EQ(state[0xC], mmio_base() + 0x168ul);
      EXPECT_EQ(state[0xD], 0ul);
      EXPECT_EQ(state[0xE], mmio_base() + 0x140ul);
      EXPECT_EQ(state[0xF], 0ul);
      EXPECT_EQ(state[0x10], mmio_base() + 0x110ul);
      EXPECT_EQ(state[0x11], 1ul << 5);
      EXPECT_EQ(state[0x12], mmio_base() + 0x1C0ul);
      EXPECT_EQ(state[0x13], 0ul);
      EXPECT_EQ(state[0x14], mmio_base() + 0x1C4ul);
      EXPECT_EQ(state[0x15], 0ul);
      EXPECT_EQ(state[0x16], mmio_base() + 0x1C8ul);
      EXPECT_EQ(state[0x17], 0ul);
      EXPECT_EQ(state[0x18], mmio_base() + 0x180ul);
      EXPECT_EQ(state[0x19], 0ul);
      EXPECT_EQ(state[0x1A], mmio_base() + 0x2B4ul);
      EXPECT_EQ(state[0x1B], 0ul);
      EXPECT_EQ(state[0x1C], 0ul);
      EXPECT_EQ(state[0x1D], 0ul);
      EXPECT_EQ(state[0x21], 0x11081011ul);
      EXPECT_EQ(state[0x22], mmio_base() + 0x3A8ul);
      EXPECT_EQ(state[0x23], 0ul);
      EXPECT_EQ(state[0x24], mmio_base() + 0x28Cul);
      EXPECT_EQ(state[0x25], 0ul);  // pdp3_upper
      EXPECT_EQ(state[0x26], mmio_base() + 0x288ul);
      EXPECT_EQ(state[0x27], 0ul);  // pdp3_lower
      EXPECT_EQ(state[0x28], mmio_base() + 0x284ul);
      EXPECT_EQ(state[0x29], 0ul);  // pdp2_upper
      EXPECT_EQ(state[0x2A], mmio_base() + 0x280ul);
      EXPECT_EQ(state[0x2B], 0ul);  // pdp2_lower
      EXPECT_EQ(state[0x2C], mmio_base() + 0x27Cul);
      EXPECT_EQ(state[0x2D], 0ul);  // pdp1_upper
      EXPECT_EQ(state[0x2E], mmio_base() + 0x278ul);
      EXPECT_EQ(state[0x2F], 0ul);  // pdp1_lower
      EXPECT_EQ(state[0x30], mmio_base() + 0x274ul);
      // EXPECT_EQ(state[0x31], pdp0_upper);
      EXPECT_EQ(state[0x32], mmio_base() + 0x270ul);
      // EXPECT_EQ(state[0x33], pdp0_lower);
      if (id() == RENDER_COMMAND_STREAMER) {
        EXPECT_EQ(state[0x41], 0x11080001ul);
        EXPECT_EQ(state[0x42], mmio_base() + 0xC8ul);
      }
      EXPECT_EQ(state[0x43], 0ul);
    } else if (DeviceId::is_gen9(device_id())) {
      EXPECT_EQ(state[1], 0x1100101Bul);
      EXPECT_EQ(state[2], mmio_base() + 0x244ul);
      switch (id()) {
        case RENDER_COMMAND_STREAMER:
          EXPECT_EQ(state[3], 0x00090009ul);
          break;
        case VIDEO_COMMAND_STREAMER:
          EXPECT_EQ(state[3], 0x00090009ul);
          break;
      }
      EXPECT_EQ(state[4], mmio_base() + 0x34ul);
      EXPECT_EQ(state[5], ringbuffer->head());
      EXPECT_EQ(state[6], mmio_base() + 0x30ul);
      EXPECT_EQ(state[7], 0u);
      EXPECT_EQ(state[8], mmio_base() + 0x38ul);
      // state[9] is not set until later
      EXPECT_EQ(state[0xA], mmio_base() + 0x3Cul);
      EXPECT_EQ(state[0xB], (31ul * PAGE_SIZE) | 1);
      EXPECT_EQ(state[0xC], mmio_base() + 0x168ul);
      EXPECT_EQ(state[0xD], 0ul);
      EXPECT_EQ(state[0xE], mmio_base() + 0x140ul);
      EXPECT_EQ(state[0xF], 0ul);
      EXPECT_EQ(state[0x10], mmio_base() + 0x110ul);
      EXPECT_EQ(state[0x11], 1ul << 5);
      EXPECT_EQ(state[0x12], mmio_base() + 0x11Cul);
      EXPECT_EQ(state[0x13], 0ul);
      EXPECT_EQ(state[0x14], mmio_base() + 0x114ul);
      EXPECT_EQ(state[0x15], 0ul);
      EXPECT_EQ(state[0x16], mmio_base() + 0x118ul);
      EXPECT_EQ(state[0x17], 0ul);
      EXPECT_EQ(state[0x18], mmio_base() + 0x1C0ul);
      EXPECT_EQ(state[0x19], 0ul);
      EXPECT_EQ(state[0x1A], mmio_base() + 0x1C4ul);
      EXPECT_EQ(state[0x1B], 0ul);
      EXPECT_EQ(state[0x1C], mmio_base() + 0x1C8ul);
      EXPECT_EQ(state[0x1D], 0ul);
      EXPECT_EQ(state[0x21], 0x11001011ul);
      EXPECT_EQ(state[0x22], mmio_base() + 0x3A8ul);
      EXPECT_EQ(state[0x23], 0ul);
      EXPECT_EQ(state[0x24], mmio_base() + 0x28Cul);
      EXPECT_EQ(state[0x25], 0ul);  // pdp3_upper
      EXPECT_EQ(state[0x26], mmio_base() + 0x288ul);
      EXPECT_EQ(state[0x27], 0ul);  // pdp3_lower
      EXPECT_EQ(state[0x28], mmio_base() + 0x284ul);
      EXPECT_EQ(state[0x29], 0ul);  // pdp2_upper
      EXPECT_EQ(state[0x2A], mmio_base() + 0x280ul);
      EXPECT_EQ(state[0x2B], 0ul);  // pdp2_lower
      EXPECT_EQ(state[0x2C], mmio_base() + 0x27Cul);
      EXPECT_EQ(state[0x2D], 0ul);  // pdp1_upper
      EXPECT_EQ(state[0x2E], mmio_base() + 0x278ul);
      EXPECT_EQ(state[0x2F], 0ul);  // pdp1_lower
      EXPECT_EQ(state[0x30], mmio_base() + 0x274ul);
      // EXPECT_EQ(state[0x31], pdp0_upper);
      EXPECT_EQ(state[0x32], mmio_base() + 0x270ul);
      // EXPECT_EQ(state[0x33], pdp0_lower);
      if (id() == RENDER_COMMAND_STREAMER) {
        EXPECT_EQ(state[0x41], 0x11000001ul);
        EXPECT_EQ(state[0x42], mmio_base() + 0xC8ul);
      }
      EXPECT_EQ(state[0x43], 0ul);
    } else {
      ASSERT_TRUE(false);
    }
  }

  void InitHardware() {
    register_io()->Write32(0,
                           engine_cs_->mmio_base() + registers::HardwareStatusPageAddress::kOffset);
    register_io()->Write32(0, engine_cs_->mmio_base() + registers::GraphicsMode::kOffset);

    engine_cs_->InitHardware();

    EXPECT_EQ(register_io()->Read32(engine_cs_->mmio_base() +
                                    registers::HardwareStatusPageAddress::kOffset),
              engine_cs_->hardware_status_page()->gpu_addr());

    if (DeviceId::is_gen12(device_id())) {
      EXPECT_NE(register_io()->Read32(engine_cs_->mmio_base() + registers::GraphicsMode::kOffset) &
                    registers::GraphicsMode::kExeclistDisableLegacyGen11,
                0u);
    } else {
      EXPECT_EQ(register_io()->Read32(engine_cs_->mmio_base() + registers::GraphicsMode::kOffset),
                0x80008000u);
    }
  }

  void RenderInit() {
    ASSERT_EQ(engine_cs_->id(), RENDER_COMMAND_STREAMER);

    auto render_cs = reinterpret_cast<RenderEngineCommandStreamer*>(engine_cs_.get());

    constexpr uint32_t kDeviceId = 0x1916;
    auto init_batch = render_cs->CreateRenderInitBatch(kDeviceId);

    if (DeviceId::is_gen9(kDeviceId)) {
      std::unique_ptr<RenderInitBatch> expected_batch =
          std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen9());
      ASSERT_NE(expected_batch, nullptr);
      EXPECT_EQ(init_batch->size(), expected_batch->size());
    }

    InitContext();

    EXPECT_TRUE(context_->Map(address_space_, engine_cs_->id()));

    auto ringbuffer = context_->get_ringbuffer(engine_cs_->id());
    ASSERT_NE(ringbuffer, nullptr);

    uint32_t tail_start = ringbuffer->tail();

    register_io_->InstallHook(std::make_unique<RegisterTracer>());

    EXPECT_TRUE(render_cs->RenderInit(context_, std::move(init_batch), address_space_));

    // Validate ringbuffer
    uint32_t expected_dwords = MiBatchBufferStart::kDwordCount + MiNoop::kDwordCount +
                               MiPipeControl::kDwordCount + MiNoop::kDwordCount +
                               MiUserInterrupt::kDwordCount;
    EXPECT_EQ(expected_dwords * 4, ringbuffer->tail() - tail_start);

    auto ringbuffer_content =
        static_cast<uint32_t*>(TestRingbuffer::vaddr(ringbuffer)) + tail_start / sizeof(uint32_t);
    ringbuffer_content = ValidateBatchBufferStart(ringbuffer_content);
    ringbuffer_content = ValidatePipeControl(ringbuffer_content, (uint32_t)kFirstSequenceNumber);

    void* addr;
    EXPECT_TRUE(TestContext::get_context_buffer(context_.get(), engine_cs_->id())
                    ->platform_buffer()
                    ->MapCpu(&addr));

    gpu_addr_t gpu_addr;
    EXPECT_TRUE(context_->GetRingbufferGpuAddress(engine_cs_->id(), &gpu_addr));

    uint32_t* state = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(addr) + PAGE_SIZE);
    EXPECT_EQ(state[6], 0x2030ul);
    EXPECT_EQ(state[7], ringbuffer->tail());
    EXPECT_EQ(state[8], 0x2038ul);
    EXPECT_EQ(state[9], gpu_addr);

    EXPECT_TRUE(TestContext::get_context_buffer(context_.get(), engine_cs_->id())
                    ->platform_buffer()
                    ->UnmapCpu());

    EXPECT_TRUE(context_->GetGpuAddress(engine_cs_->id(), &gpu_addr));

    uint32_t upper_32_bits = static_cast<uint32_t>(gpu_addr >> 12);
    uint32_t lower_32_bits = static_cast<uint32_t>(gpu_addr | 0x19);
    std::vector<uint32_t> submitport_writes{0, 0, upper_32_bits, lower_32_bits};
    uint32_t write = 0;

    for (auto operation : static_cast<RegisterTracer*>(register_io_->hook())->trace()) {
      if (operation.offset == EngineCommandStreamer::kRenderEngineMmioBase +
                                  registers::ExeclistSubmitPort::kSubmitOffset) {
        EXPECT_EQ(operation.type, RegisterTracer::Operation::WRITE32);
        ASSERT_LT(write, submitport_writes.size());
        EXPECT_EQ(operation.val, submitport_writes[write++]);
      }
    }
    EXPECT_EQ(write, submitport_writes.size());

    EXPECT_TRUE(context_->Unmap(engine_cs_->id()));
  }

  void InitIndirectContext() {
    ASSERT_EQ(engine_cs_->id(), RENDER_COMMAND_STREAMER);

    InitContext();

    EXPECT_TRUE(context_->Map(address_space_, engine_cs_->id()));

    auto render_cs = reinterpret_cast<RenderEngineCommandStreamer*>(engine_cs_.get());

    std::shared_ptr<IndirectContextBatch> indirect_context_batch =
        render_cs->CreateIndirectContextBatch(address_space_);
    ASSERT_TRUE(indirect_context_batch);

    render_cs->InitIndirectContext(context_.get(), indirect_context_batch);

    void* addr;
    EXPECT_TRUE(TestContext::get_context_buffer(context_.get(), engine_cs_->id())
                    ->platform_buffer()
                    ->MapCpu(&addr));

    uint32_t* state =
        reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(addr) + magma::page_size());

    if (DeviceId::is_gen12(device_id())) {
      EXPECT_EQ(state[0x14], EngineCommandStreamer::kRenderEngineMmioBase + 0x1C4);
      EXPECT_EQ(state[0x15] & ~0x3F, indirect_context_batch->GetBatchMapping()->gpu_addr());
      EXPECT_EQ(state[0x15] & 0x3F, indirect_context_batch->length() / DeviceId::cache_line_size());
      EXPECT_EQ(state[0x16], EngineCommandStreamer::kRenderEngineMmioBase + 0x1C8);
      EXPECT_EQ(state[0x17], 0x0Du << 6);
    } else {
      EXPECT_EQ(state[0x1A], EngineCommandStreamer::kRenderEngineMmioBase + 0x1C4);
      EXPECT_EQ(state[0x1B] & ~0x3F, indirect_context_batch->GetBatchMapping()->gpu_addr());
      EXPECT_EQ(state[0x1B] & 0x3F, indirect_context_batch->length() / DeviceId::cache_line_size());
      EXPECT_EQ(state[0x1C], EngineCommandStreamer::kRenderEngineMmioBase + 0x1C8);
      EXPECT_EQ(state[0x1D], 0x26u << 6);
    }

    EXPECT_TRUE(TestContext::get_context_buffer(context_.get(), engine_cs_->id())
                    ->platform_buffer()
                    ->UnmapCpu());
  }

  void MoveBatchToInflight() {
    EXPECT_TRUE(engine_cs_->InitContext(context_.get()));
    EXPECT_TRUE(context_->Map(address_space_, engine_cs_->id()));

    auto ringbuffer = context_->get_ringbuffer(engine_cs_->id());
    ASSERT_NE(nullptr, ringbuffer);
    uint32_t tail_start = ringbuffer->tail();

    gpu_addr_t gpu_addr = 0x10000;  // Arbitrary
    EXPECT_TRUE(
        engine_cs_->MoveBatchToInflight(std::make_unique<MockMappedBatch>(context_, gpu_addr)));

    // Validate ringbuffer
    uint32_t dword_count = 0;
    dword_count += MiBatchBufferStart::kDwordCount + MiNoop::kDwordCount;
    if (engine_cs_->id() == RENDER_COMMAND_STREAMER) {
      dword_count +=
          MiPipeControl::kDwordCount + MiNoop::kDwordCount + MiUserInterrupt::kDwordCount;
    } else {
      dword_count += MiFlush::kDwordCount + MiUserInterrupt::kDwordCount;
    }

    ASSERT_EQ(ringbuffer->tail() - tail_start, dword_count * sizeof(uint32_t));

    uint32_t* ringbuffer_content =
        TestRingbuffer::vaddr(ringbuffer) + tail_start / sizeof(uint32_t);
    ringbuffer_content = ValidateBatchBufferStart(ringbuffer_content);
    if (engine_cs_->id() == RENDER_COMMAND_STREAMER) {
      ringbuffer_content = ValidatePipeControl(ringbuffer_content, kFirstSequenceNumber);
    } else {
      ringbuffer_content = ValidateFlush(ringbuffer_content, kFirstSequenceNumber);
    }
  }

  void MappingRelease() {
    EXPECT_TRUE(engine_cs_->InitContext(context_.get()));
    EXPECT_TRUE(context_->Map(address_space_, engine_cs_->id()));

    auto ringbuffer = context_->get_ringbuffer(engine_cs_->id());
    ASSERT_NE(nullptr, ringbuffer);
    uint32_t tail_start = ringbuffer->tail();

    std::shared_ptr<GpuMapping> mapping =
        AddressSpace::MapBufferGpu(address_space_, MsdIntelBuffer::Create(PAGE_SIZE, "test"));

    std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mappings;
    mapping->Release(&bus_mappings);

    {
      auto wrapper = std::make_shared<MappingReleaseBatch::BusMappingsWrapper>();
      wrapper->bus_mappings = std::move(bus_mappings);

      auto render_cs = reinterpret_cast<RenderEngineCommandStreamer*>(engine_cs_.get());

      auto batch = std::make_unique<MappingReleaseBatch>(std::move(wrapper));
      batch->SetContext(context_);

      render_cs->MoveBatchToInflight(std::move(batch));
    }

    // Validate ringbuffer
    uint32_t dword_count = 0;
    if (engine_cs_->id() == RENDER_COMMAND_STREAMER) {
      dword_count += MiPipeControl::kDwordCount + MiNoop::kDwordCount;
    } else {
      dword_count += MiFlush::kDwordCount;
    }
    dword_count += MiUserInterrupt::kDwordCount;
    ASSERT_EQ(ringbuffer->tail() - tail_start, dword_count * sizeof(uint32_t));

    uint32_t* ringbuffer_content =
        TestRingbuffer::vaddr(ringbuffer) + tail_start / sizeof(uint32_t);
    if (engine_cs_->id() == RENDER_COMMAND_STREAMER) {
      ringbuffer_content = ValidatePipeControl(ringbuffer_content, kFirstSequenceNumber);
    } else {
      ringbuffer_content = ValidateFlush(ringbuffer_content, kFirstSequenceNumber);
    }
  }

  void Reset() {
    class Hook : public magma::RegisterIo::Hook {
     public:
      Hook(MsdIntelRegisterIo* register_io, EngineCommandStreamerId id, uint32_t device_id)
          : register_io_(register_io), id_(id), device_id_(device_id) {}

      constexpr uint32_t mask(EngineCommandStreamerId id) {
        switch (id) {
          case RENDER_COMMAND_STREAMER:
            return 1 << registers::GraphicsDeviceResetControl::kRcsResetBit;
          case VIDEO_COMMAND_STREAMER:
            if (DeviceId::is_gen12(device_id_)) {
              return 1 << registers::GraphicsDeviceResetControl::kVcs0ResetBitGen12;
            } else {
              return 1 << registers::GraphicsDeviceResetControl::kVcsResetBit;
            }
        }
        return 0;
      }

      void Write32(uint32_t val, uint32_t offset) override {
        switch (offset) {
          case EngineCommandStreamer::kRenderEngineMmioBase + registers::ResetControl::kOffset:
          case EngineCommandStreamer::kVideoEngineMmioBase + registers::ResetControl::kOffset:
          case EngineCommandStreamer::kVideoEngineMmioBaseGen12 + registers::ResetControl::kOffset:
            // set ready for reset bit
            if (val & 0x00010001) {
              val = register_io_->mmio()->Read32(offset) | 0x2;
              register_io_->mmio()->Write32(val, offset);
            }
            break;
          case registers::GraphicsDeviceResetControl::kOffset:
            // clear the reset bit
            if (val & mask(id_)) {
              val = register_io_->mmio()->Read32(offset) & ~mask(id_);
              register_io_->mmio()->Write32(val, offset);
            }
            break;
        }
      }

      void Read32(uint32_t val, uint32_t offset) override {}
      void Read64(uint64_t val, uint32_t offset) override {}

     private:
      MsdIntelRegisterIo* register_io_;
      EngineCommandStreamerId id_;
      uint32_t device_id_;
    };

    register_io_->InstallHook(
        std::make_unique<Hook>(register_io_.get(), engine_cs_->id(), device_id()));

    EXPECT_TRUE(engine_cs_->Reset());
  }

  uint32_t* ValidateBatchBufferStart(uint32_t* ringbuffer_content) {
    auto render_cs = reinterpret_cast<RenderEngineCommandStreamer*>(engine_cs_.get());

    gpu_addr_t batch_addr;
    EXPECT_TRUE(
        render_cs->inflight_command_sequences_.back().mapped_batch()->GetGpuAddress(&batch_addr));

    // Subtract 2 from dword count as per the instruction definition
    EXPECT_EQ(*ringbuffer_content++,
              MiBatchBufferStart::kCommandType | (MiBatchBufferStart::kDwordCount - 2));
    EXPECT_EQ(*ringbuffer_content++, magma::lower_32_bits(batch_addr));
    EXPECT_EQ(*ringbuffer_content++, magma::upper_32_bits(batch_addr));
    EXPECT_EQ(*ringbuffer_content++, (uint32_t)MiNoop::kCommandType);
    return ringbuffer_content;
  }

  uint32_t* ValidatePipeControl(uint32_t* ringbuffer_content, uint32_t sequence_number) {
    gpu_addr_t seqno_gpu_addr = engine_cs_->hardware_status_page()->gpu_addr() +
                                GlobalHardwareStatusPage::kSequenceNumberOffset;

    // Subtract 2 from dword count as per the instruction definition
    EXPECT_EQ(*ringbuffer_content++, 0x7A000000u | (MiPipeControl::kDwordCount - 2));
    EXPECT_EQ(*ringbuffer_content++,
              MiPipeControl::kPostSyncWriteImmediateBit | MiPipeControl::kAddressSpaceGlobalGttBit);
    EXPECT_EQ(*ringbuffer_content++, magma::lower_32_bits(seqno_gpu_addr));
    EXPECT_EQ(*ringbuffer_content++, magma::upper_32_bits(seqno_gpu_addr));
    EXPECT_EQ(*ringbuffer_content++, sequence_number);
    return ringbuffer_content;
  }

  uint32_t* ValidateFlush(uint32_t* ringbuffer_content, uint32_t sequence_number) {
    gpu_addr_t seqno_gpu_addr = engine_cs_->hardware_status_page()->gpu_addr() +
                                GlobalHardwareStatusPage::kSequenceNumberOffset;

    // Subtract 2 from dword count as per the instruction definition
    constexpr uint32_t kCommandFlushHeader = MiFlush::kCommandType | MiFlush::kCommandOpcode |
                                             MiFlush::kPostSyncWriteImmediateBit |
                                             MiFlush::kDwordCount - 2;
    EXPECT_EQ(*ringbuffer_content++, kCommandFlushHeader);
    EXPECT_EQ(*ringbuffer_content++,
              magma::lower_32_bits(seqno_gpu_addr) | MiFlush::kAddressSpaceGlobalGttBit);
    EXPECT_EQ(*ringbuffer_content++, magma::upper_32_bits(seqno_gpu_addr));
    EXPECT_EQ(*ringbuffer_content++, sequence_number);
    EXPECT_EQ(*ringbuffer_content++, 0u);
    return ringbuffer_content;
  }

  uint32_t device_id() override { return GetParam().device_id; }

  MsdIntelRegisterIo* register_io() override { return register_io_.get(); }

  Sequencer* sequencer() override { return sequencer_.get(); }

  magma::PlatformPciDevice* platform_device() override {
    DASSERT(false);
    return nullptr;
  }

  magma::PlatformBusMapper* GetBusMapper() override {
    DASSERT(false);
    return nullptr;
  }

 private:
  std::unique_ptr<MsdIntelRegisterIo> register_io_;
  std::unique_ptr<AddressSpaceOwner> address_space_owner_;
  std::shared_ptr<AddressSpace> address_space_;
  std::shared_ptr<MsdIntelContext> context_;
  std::unique_ptr<EngineCommandStreamer> engine_cs_;
  std::unique_ptr<Sequencer> sequencer_;
};

TEST_P(TestEngineCommandStreamer, InitContext) { TestEngineCommandStreamer::InitContext(); }

TEST_P(TestEngineCommandStreamer, InitHardware) { TestEngineCommandStreamer::InitHardware(); }

TEST_P(TestEngineCommandStreamer, RenderInitGen9) {
  if (id() != RENDER_COMMAND_STREAMER || !DeviceId::is_gen9(device_id())) {
    GTEST_SKIP();
  }
  TestEngineCommandStreamer::RenderInit();
}

TEST_P(TestEngineCommandStreamer, IndirectContext) {
  if (id() != RENDER_COMMAND_STREAMER) {
    GTEST_SKIP();
  }
  TestEngineCommandStreamer::InitIndirectContext();
}

TEST_P(TestEngineCommandStreamer, Reset) { TestEngineCommandStreamer::Reset(); }

TEST_P(TestEngineCommandStreamer, MoveBatchToInflight) {
  TestEngineCommandStreamer::MoveBatchToInflight();
}

TEST_P(TestEngineCommandStreamer, MappingRelease) { TestEngineCommandStreamer::MappingRelease(); }

INSTANTIATE_TEST_SUITE_P(
    TestEngineCommandStreamer, TestEngineCommandStreamer,
    testing::Values(TestParam{.id = RENDER_COMMAND_STREAMER, .device_id = 0x5916},
                    TestParam{.id = VIDEO_COMMAND_STREAMER, .device_id = 0x5916},
                    TestParam{.id = RENDER_COMMAND_STREAMER, .device_id = 0x9A49},
                    TestParam{.id = VIDEO_COMMAND_STREAMER, .device_id = 0x9A49}),
    [](const testing::TestParamInfo<TestEngineCommandStreamer::ParamType>& info) {
      std::string name;
      switch (info.param.id) {
        case RENDER_COMMAND_STREAMER:
          name += "Render";
          break;
        case VIDEO_COMMAND_STREAMER:
          name += "Video";
          break;
      }
      if (DeviceId::is_gen12(info.param.device_id)) {
        name += "Gen12";
      } else if (DeviceId::is_gen9(info.param.device_id)) {
        name += "Gen9";
      }
      return name;
    });
