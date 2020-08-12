// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "cmdstream_fuchsia.h"
int etnaviv_cl_test_gc7000(int argc, char* argv[]);
}

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "magma_util/macros.h"
#include "src/graphics/drivers/msd-vsi-vip/src/address_space.h"
#include "src/graphics/drivers/msd-vsi-vip/src/command_buffer.h"
#include "src/graphics/drivers/msd-vsi-vip/src/gpu_mapping.h"
#include "src/graphics/drivers/msd-vsi-vip/src/instructions.h"
#include "src/graphics/drivers/msd-vsi-vip/src/msd_vsi_device.h"
#include "src/graphics/drivers/msd-vsi-vip/tests/mock/mock_mapped_batch.h"

TEST(MsdVsiDevice, MemoryWrite) { EXPECT_EQ(0, etnaviv_cl_test_gc7000(0, nullptr)); }

class TestMsdVsiDevice : public drm_test_info {
 public:
  static constexpr uint32_t kAddressSpaceIndex = 1;

  bool Init() {
    DLOG("init begin");

    device_.test = command_stream_.test = this;

    this->dev = &device_;
    this->stream = &command_stream_;

    device_.msd_vsi_device =
        MsdVsiDevice::Create(GetTestDeviceHandle(), true /* start_device_thread */);
    if (!device_.msd_vsi_device)
      return DRETF(false, "no test device");

    if (!device_.msd_vsi_device->IsIdle())
      return DRETF(false, "device not idle");

    address_space_owner_ =
        std::make_unique<AddressSpaceOwner>(device_.msd_vsi_device->GetBusMapper());
    address_space_ = AddressSpace::Create(address_space_owner_.get(), kAddressSpaceIndex);
    if (!address_space_)
      return DRETF(false, "failed to create address space");

    device_.msd_vsi_device->page_table_arrays()->AssignAddressSpace(kAddressSpaceIndex,
                                                                    address_space_.get());

    std::weak_ptr<MsdVsiConnection> connection;
    context_ =
        MsdVsiContext::Create(connection, address_space_, device_.msd_vsi_device->GetRingbuffer());
    EXPECT_NE(context_, nullptr);

    command_stream_.etna_buffer =
        static_cast<EtnaBuffer*>(etna_bo_new(this->dev, PAGE_SIZE, DRM_ETNA_GEM_CACHE_UNCACHED));
    if (!command_stream_.etna_buffer)
      return DRETF(false, "failed to get command stream buffer");

    if (!command_stream_.etna_buffer->msd_buffer->platform_buffer()->MapCpu(
            reinterpret_cast<void**>(&command_stream_.cmd_ptr)))
      return DRETF(false, "failed to map cmd_ptr");

    DLOG("init complete");

    return true;
  }

  void StopRingbuffer() {
    device()->StopRingbuffer();

    auto start = std::chrono::high_resolution_clock::now();
    while (!device()->IsIdle() && std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::high_resolution_clock::now() - start)
                                          .count() < 1000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto reg = registers::IdleState::Get().ReadFrom(register_io());
    EXPECT_EQ(0x7FFFFFFFu, reg.reg_value());
  }

  struct EtnaDevice : public etna_dev {
    std::unique_ptr<MsdVsiDevice> msd_vsi_device;
    TestMsdVsiDevice* test = nullptr;
  };

  struct EtnaBuffer : public etna_bo {
    std::shared_ptr<MsdVsiBuffer> msd_buffer;
    uint32_t gpu_addr = 0xFAFAFAFA;
  };

  struct EtnaCommandStream : public etna_cmd_stream {
    EtnaBuffer* etna_buffer = nullptr;
    uint32_t* cmd_ptr = nullptr;
    uint32_t index = 0;
    TestMsdVsiDevice* test = nullptr;
  };

  MsdVsiDevice* device() { return device_.msd_vsi_device.get(); }
  Ringbuffer* ringbuffer() { return device()->ringbuffer_.get(); }

  magma::PlatformBusMapper* GetBusMapper() { return device_.msd_vsi_device->GetBusMapper(); }

  magma::RegisterIo* register_io() { return device_.msd_vsi_device->register_io(); }

  std::shared_ptr<MsdVsiContext> context() { return context_; }
  std::shared_ptr<AddressSpace> address_space() { return address_space_; }

  bool AllocInterruptEvent(uint32_t* out_id) {
    return device_.msd_vsi_device->AllocInterruptEvent(false /* free_on_complete */, out_id);
  }
  bool FreeInterruptEvent(uint32_t id) { return device_.msd_vsi_device->FreeInterruptEvent(id); }

  bool SubmitCommandBuffer(TestMsdVsiDevice::EtnaBuffer* etna_buf, uint32_t length,
                           std::shared_ptr<magma::PlatformSemaphore> signal) {
    auto command_buffer = std::make_unique<magma_system_command_buffer>(magma_system_command_buffer{
        .batch_buffer_resource_index = 0,
        .batch_start_offset = 0,
        .num_resources = 1,
        .wait_semaphore_count = 0,
        .signal_semaphore_count = 1,
    });
    auto batch = std::make_unique<CommandBuffer>(context_, 0, std::move(command_buffer));
    EXPECT_NE(batch, nullptr);

    std::vector<CommandBuffer::ExecResource> resources;
    resources.emplace_back(
        CommandBuffer::ExecResource{.buffer = etna_buf->msd_buffer, .offset = 0, .length = length});

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
    signal_semaphores.emplace_back(signal);
    if (!batch->InitializeResources(std::move(resources), std::move(wait_semaphores),
                                    std::move(signal_semaphores))) {
      return DRETF(false, "failed to initialize command buffer resources");
    }
    if (!batch->PrepareForExecution()) {
      return DRETF(false, "failed to prepare command buffer for execution");
    }
    if (!batch->IsValidBatch()) {
      return DRETF(false, "failed to validate batch buffer");
    }
    if (!device_.msd_vsi_device->SubmitBatch(std::move(batch), false /* do_flush */).ok()) {
      return DRETF(false, "failed to submit batch");
    }
    return true;
  }

  uint32_t next_gpu_addr(uint32_t size) {
    uint32_t next = next_gpu_addr_;
    next_gpu_addr_ += size;
    return next;
  }

 private:
  class AddressSpaceOwner : public AddressSpace::Owner {
   public:
    AddressSpaceOwner(magma::PlatformBusMapper* bus_mapper) : bus_mapper_(bus_mapper) {}
    virtual ~AddressSpaceOwner() = default;

    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_; }

    void AddressSpaceReleased(AddressSpace* address_space) override {}

   private:
    magma::PlatformBusMapper* bus_mapper_;
  };

  EtnaDevice device_;  // Device should be destroyed last.
  EtnaCommandStream command_stream_;

  std::shared_ptr<MsdVsiContext> context_;
  std::unique_ptr<AddressSpaceOwner> address_space_owner_;
  std::shared_ptr<AddressSpace> address_space_;
  uint32_t next_gpu_addr_ = 0x10000;
};

struct drm_test_info* drm_test_setup(int argc, char** argv) {
  auto test_info = std::make_unique<TestMsdVsiDevice>();
  if (!test_info->Init())
    return DRETP(nullptr, "failed to init test");
  return test_info.release();
}

void drm_test_teardown(struct drm_test_info* info) {
  auto msd_device = static_cast<TestMsdVsiDevice*>(info);
  msd_device->StopRingbuffer();
  delete static_cast<TestMsdVsiDevice*>(info);
}

void etna_set_state(struct etna_cmd_stream* stream, uint32_t address, uint32_t value) {
  DLOG("set state 0x%x 0x%x", address, value);
  auto cmd_stream = static_cast<TestMsdVsiDevice::EtnaCommandStream*>(stream);

  cmd_stream->cmd_ptr[cmd_stream->index++] = (1 << 27)          // load state
                                             | (1 << 16)        // count
                                             | (address >> 2);  // register to be written
  cmd_stream->cmd_ptr[cmd_stream->index++] = value;
}

void etna_set_state_from_bo(struct etna_cmd_stream* stream, uint32_t address, struct etna_bo* bo,
                            uint32_t reloc_flags) {
  DLOG("set state from bo 0x%x gpu_addr 0x%x", address,
       static_cast<TestMsdVsiDevice::EtnaBuffer*>(bo)->gpu_addr);
  auto cmd_stream = static_cast<TestMsdVsiDevice::EtnaCommandStream*>(stream);

  cmd_stream->cmd_ptr[cmd_stream->index++] = (1 << 27)          // load state
                                             | (1 << 16)        // count
                                             | (address >> 2);  // register to be written
  cmd_stream->cmd_ptr[cmd_stream->index++] =
      static_cast<TestMsdVsiDevice::EtnaBuffer*>(bo)->gpu_addr;
}

void etna_stall(struct etna_cmd_stream* stream, uint32_t from, uint32_t to) {
  DLOG("stall %u %u", from, to);
  auto cmd_stream = static_cast<TestMsdVsiDevice::EtnaCommandStream*>(stream);

  etna_set_state(stream, 0x00003808, (from & 0x1f) | ((to << 8) & 0x1f00));

  if (from == 1) {  // FE
    cmd_stream->cmd_ptr[cmd_stream->index++] = 0x48000000;
    cmd_stream->cmd_ptr[cmd_stream->index++] = (from & 0x1f) | ((to << 8) & 0x1f00);
  } else {
    DASSERT(false);
  }
}

// Create a buffer and map it into the gpu address space.
struct etna_bo* etna_bo_new(void* dev, uint32_t size, uint32_t flags) {
  DLOG("bo new size %u flags 0x%x", size, flags);
  auto etna_buffer = std::make_unique<TestMsdVsiDevice::EtnaBuffer>();

  std::unique_ptr<magma::PlatformBuffer> buffer = magma::PlatformBuffer::Create(size, "EtnaBuffer");
  if (!buffer)
    return DRETP(nullptr, "failed to alloc buffer size %u", size);

  if (flags & DRM_ETNA_GEM_CACHE_UNCACHED)
    buffer->SetCachePolicy(MAGMA_CACHE_POLICY_WRITE_COMBINING);

  auto etna_device = static_cast<TestMsdVsiDevice::EtnaDevice*>(dev);
  uint32_t page_count = buffer->size() / PAGE_SIZE;

  etna_buffer->gpu_addr = etna_device->test->next_gpu_addr(buffer->size());

  etna_buffer->msd_buffer = std::make_unique<MsdVsiBuffer>(std::move(buffer));

  std::shared_ptr<GpuMapping> gpu_mapping;
  magma::Status status =
      AddressSpace::MapBufferGpu(etna_device->test->address_space(), etna_buffer->msd_buffer,
                                 etna_buffer->gpu_addr, 0, page_count, &gpu_mapping);

  if (!status.ok()) {
    return DRETP(nullptr, "failed to map buffer");
  }

  if (!etna_device->test->address_space()->AddMapping(gpu_mapping))
    return DRETP(nullptr, "couldn't add mapping to address space");

  return etna_buffer.release();
}

void* etna_bo_map(struct etna_bo* bo) {
  DLOG("bo map %p", bo);
  void* addr;
  if (!static_cast<TestMsdVsiDevice::EtnaBuffer*>(bo)->msd_buffer->platform_buffer()->MapCpu(&addr))
    return DRETP(nullptr, "Failed to map etna buffer");
  DLOG("bo map returning %p", addr);
  return addr;
}

// Returns true if the |gpu_addr| lies between the addresses of the last WAIT-LINK command.
bool matches_last_wait_link(std::shared_ptr<MsdVsiContext> context, Ringbuffer* ringbuffer,
                            uint32_t gpu_addr) {
  // The last WAIT-LINK will be between [tail - 16, tail].
  auto wait_link_start = ringbuffer->SubtractOffset(kWaitLinkDwords * sizeof(uint32_t));
  auto wait_link_end = ringbuffer->tail();

  uint64_t rb_gpu_addr;
  if (!context->exec_address_space()->GetRingbufferGpuAddress(&rb_gpu_addr)) {
    return DRETF(false, "Failed to get ringbuffer gpu addr");
  }
  // The address lies before the start of the ringbuffer.
  if (gpu_addr < rb_gpu_addr) {
    return false;
  }
  auto rb_offset = gpu_addr - rb_gpu_addr;
  if (rb_offset >= ringbuffer->size()) {
    return false;
  }
  return wait_link_start <= wait_link_end
             ? (rb_offset >= wait_link_start) && (rb_offset < wait_link_end)
             : (rb_offset >= wait_link_start) || (rb_offset < wait_link_end);
}

void etna_cmd_stream_finish(struct etna_cmd_stream* stream) {
  auto cmd_stream = static_cast<TestMsdVsiDevice::EtnaCommandStream*>(stream);

  uint32_t length = cmd_stream->index * sizeof(uint32_t);

  DLOG("etna_cmd_stream_finish length %u", length);

  auto semaphore = magma::PlatformSemaphore::Create();
  EXPECT_NE(semaphore, nullptr);

  EXPECT_TRUE(
      cmd_stream->test->SubmitCommandBuffer(cmd_stream->etna_buffer, length, semaphore->Clone()));
  auto start = std::chrono::high_resolution_clock::now();

  // When the command buffer completes, we expect to return back to the next WAIT-LINK
  // in the ringbuffer. Wait until that happens or we timeout.
  constexpr uint64_t kTimeoutMs = 1000;
  EXPECT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());

  {
    auto dma_addr = registers::DmaAddress::Get().ReadFrom(cmd_stream->test->register_io());
    EXPECT_TRUE(matches_last_wait_link(cmd_stream->test->context(), cmd_stream->test->ringbuffer(),
                                       dma_addr.reg_value()));
    DLOG("dma_addr 0x%x", dma_addr.reg_value());
  }

  {
    // The ringbuffer should be in WAIT-LINK until we explicitly stop it.
    auto reg = registers::IdleState::Get().ReadFrom(cmd_stream->test->register_io());
    EXPECT_NE(0x7FFFFFFFu, reg.reg_value());
  }

  DLOG("execution took %lld ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::high_resolution_clock::now() - start)
                                     .count());
  {
    auto reg = registers::MmuSecureStatus::Get().ReadFrom(cmd_stream->test->register_io());
    EXPECT_EQ(0u, reg.reg_value());
  }
  {
    auto reg =
        registers::MmuSecureExceptionAddress::Get().ReadFrom(cmd_stream->test->register_io());
    EXPECT_EQ(0u, reg.reg_value());
  }
}
