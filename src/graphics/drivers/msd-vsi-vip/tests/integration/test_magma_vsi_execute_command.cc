// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magma.h>
#include <stdio.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "test_magma_vsi.h"

extern "C" {
#include "cmdstream_fuchsia.h"
}

namespace {

// Provided by etnaviv_cl_test_gc7000.c
extern "C" uint32_t hello_code[];
extern "C" void gen_cmd_stream(struct etna_cmd_stream* stream, struct etna_bo* code,
                               struct etna_bo* bmp);

class MagmaExecuteMsdVsi : public testing::Test {
 protected:
  void SetUp() override {
    magma_vsi_.DeviceFind();
    magma_vsi_.ConnectionCreate();
    magma_vsi_.ContextCreate();
  }

  void TearDown() override {
    magma_vsi_.ContextRelease();
    magma_vsi_.ConnectionRelease();
    magma_vsi_.DeviceClose();
  }

 public:
  class EtnaBuffer : public etna_bo {
    friend MagmaExecuteMsdVsi;

   public:
    uint32_t* GetCpuAddress() const { return reinterpret_cast<uint32_t*>(cpu_address_); }

   private:
    magma_buffer_t magma_buffer_;
    uint32_t size_;
    uint64_t gpu_address_;
    magma_system_exec_resource resource_;
    void* cpu_address_ = nullptr;
  };

  std::shared_ptr<EtnaBuffer> CreateEtnaBuffer(uint32_t size) {
    auto etna_buffer = std::make_shared<EtnaBuffer>();
    uint64_t actual_size = 0;

    if (MAGMA_STATUS_OK != magma_create_buffer(magma_vsi_.GetConnection(), size, &actual_size,
                                               &(etna_buffer->magma_buffer_)))
      return nullptr;

    EXPECT_EQ(actual_size, size);
    EXPECT_NE(etna_buffer->magma_buffer_, 0ul);

    EXPECT_EQ(MAGMA_STATUS_OK, magma_set_cache_policy(etna_buffer->magma_buffer_,
                                                      MAGMA_CACHE_POLICY_WRITE_COMBINING));

    if (MAGMA_STATUS_OK != magma_map(magma_vsi_.GetConnection(), etna_buffer->magma_buffer_,
                                     &etna_buffer->cpu_address_))
      return nullptr;

    etna_buffer->size_ = magma_get_buffer_size(etna_buffer->magma_buffer_);

    uint64_t page_count = etna_buffer->size_ / PAGE_SIZE;
    EXPECT_NE(page_count, 0ul);

    etna_buffer->gpu_address_ = next_gpu_addr_;
    next_gpu_addr_ += etna_buffer->size_;

    magma_map_buffer_gpu(magma_vsi_.GetConnection(), etna_buffer->magma_buffer_,
                         0,  // page offset
                         page_count, etna_buffer->gpu_address_,
                         0  // flags
    );

    etna_buffer->resource_.buffer_id = magma_get_buffer_id(etna_buffer->magma_buffer_);
    etna_buffer->resource_.offset = 0;
    etna_buffer->resource_.length = etna_buffer->size_;

    return etna_buffer;
  }

  class EtnaCommandStream : public etna_cmd_stream {
    friend MagmaExecuteMsdVsi;

   public:
    void EtnaSetState(uint32_t address, uint32_t value) {
      WriteCommand((1 << 27)           // load state
                   | (1 << 16)         // count
                   | (address >> 2));  // register to be written
      WriteCommand(value);
    }

    void EtnaSetStateFromBuffer(uint32_t address, const EtnaBuffer& buffer, uint32_t reloc_flags) {
      WriteCommand((1 << 27)           // load state
                   | (1 << 16)         // count
                   | (address >> 2));  // register to be written
      WriteCommand(buffer.gpu_address_);
    }

    void EtnaStall(uint32_t from, uint32_t to) {
      EtnaSetState(0x00003808, (from & 0x1f) | ((to << 8) & 0x1f00));

      ASSERT_EQ(from, 1u);

      WriteCommand(0x48000000);
      WriteCommand((from & 0x1f) | ((to << 8) & 0x1f00));
    }

    void EtnaLink(uint16_t prefetch, uint32_t gpu_address) {
      constexpr uint32_t kLinkCommand = 0x40000000;
      WriteCommand(kLinkCommand | prefetch);
      WriteCommand(gpu_address);
    }

   protected:
    std::shared_ptr<EtnaBuffer> etna_buffer = nullptr;
    uint32_t index = 0;

    void WriteCommand(uint32_t command) {
      ASSERT_NE(etna_buffer, nullptr);
      ASSERT_LT(index, etna_buffer->size_);

      etna_buffer->GetCpuAddress()[index++] = command;
      etna_buffer->resource_.length = index * sizeof(uint32_t);
    }
  };

  std::unique_ptr<EtnaCommandStream> CreateEtnaCommandStream(uint32_t size) {
    auto command_stream = std::make_unique<EtnaCommandStream>();

    command_stream->etna_buffer = CreateEtnaBuffer(size);
    if (!command_stream->etna_buffer)
      return nullptr;

    command_stream->etna_buffer->resource_.length = 0;

    return command_stream;
  }

  void ExecuteCommand(std::shared_ptr<EtnaCommandStream> command_stream, uint32_t timeout) {
    uint32_t length = command_stream->index * sizeof(uint32_t);
    magma_semaphore_t semaphore;

    ASSERT_NE(length, 0u);
    ASSERT_EQ(magma_create_semaphore(magma_vsi_.GetConnection(), &semaphore), MAGMA_STATUS_OK);
    uint64_t semaphore_id = magma_get_semaphore_id(semaphore);

    std::vector<magma_system_exec_resource> resources;
    resources.push_back(command_stream->etna_buffer->resource_);

    magma_system_command_buffer command_buffer = {
        .resource_count = static_cast<uint32_t>(resources.size()),
        .batch_buffer_resource_index = 0,
        .batch_start_offset = 0,
        .wait_semaphore_count = 0,
        .signal_semaphore_count = 1};

    EXPECT_NE(resources[0].length, 0ul);

    auto start = std::chrono::high_resolution_clock::now();

    magma_execute_command_buffer_with_resources(magma_vsi_.GetConnection(),
                                                magma_vsi_.GetContextId(), &command_buffer,
                                                resources.data(), &semaphore_id);
    ASSERT_EQ(magma_wait_semaphores(&semaphore, 1, timeout, true), MAGMA_STATUS_OK);

    auto t = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::high_resolution_clock::now() - start)
                 .count();
    EXPECT_LT(t, timeout);

    magma_release_semaphore(magma_vsi_.GetConnection(), semaphore);
  }

  void Test() {
    static constexpr size_t kCodeSize = 4096;

    std::shared_ptr<EtnaCommandStream> command_stream = CreateEtnaCommandStream(kCodeSize);
    ASSERT_TRUE(command_stream);

    std::shared_ptr<EtnaBuffer> code = CreateEtnaBuffer(kCodeSize);
    ASSERT_TRUE(code);

    bool found_end_of_code = false;
    for (uint32_t i = 0; i < kCodeSize / sizeof(uint32_t); i++) {
      if ((i % 4 == 0) && hello_code[i] == 0) {
        // End of code is a NOOP line
        found_end_of_code = true;
        break;
      }
      code->GetCpuAddress()[i] = hello_code[i];
    }
    EXPECT_TRUE(found_end_of_code);

    static constexpr size_t kBufferSize = 65536;
    std::shared_ptr<EtnaBuffer> output_buffer = CreateEtnaBuffer(kBufferSize);
    ASSERT_TRUE(output_buffer);

    // Memset doesn't like uncached buffers
    for (uint32_t i = 0; i < kBufferSize / sizeof(uint32_t); i++) {
      output_buffer->GetCpuAddress()[i] = 0;
    }

    gen_cmd_stream(command_stream.get(), code.get(), output_buffer.get());

    static constexpr uint32_t kTimeoutMs = 10;
    ExecuteCommand(command_stream, kTimeoutMs);

    auto data = reinterpret_cast<const char*>(output_buffer->GetCpuAddress());
    ASSERT_TRUE(data);

    const char kHelloWorld[] = "Hello, World!";
    EXPECT_STREQ(data, kHelloWorld);
  }

  void TestExecuteMmuException() {
    static constexpr size_t kCodeSize = 4096;

    std::shared_ptr<EtnaCommandStream> command_stream = CreateEtnaCommandStream(kCodeSize);
    ASSERT_TRUE(command_stream);

    // Jump to an unmapped address.
    command_stream->EtnaLink(0x8 /* arbitrary prefetch */, next_gpu_addr_);

    static constexpr uint32_t kTimeoutMs = 10;
    ExecuteCommand(command_stream, kTimeoutMs);

    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, magma_get_error(magma_vsi_.GetConnection()));
  }

  void TestHang() {
    static constexpr size_t kCodeSize = 4096;

    std::shared_ptr<EtnaCommandStream> command_stream = CreateEtnaCommandStream(kCodeSize);
    ASSERT_TRUE(command_stream);

    // Infinite loop by jumping back to the link command.
    command_stream->EtnaLink(0x8 /* prefetch */, command_stream->etna_buffer->gpu_address_);

    static constexpr uint32_t kTimeoutMs = 6000;
    ExecuteCommand(command_stream, kTimeoutMs);

    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, magma_get_error(magma_vsi_.GetConnection()));
  }

 private:
  MagmaVsi magma_vsi_;

  uint64_t next_gpu_addr_ = 0x10000;
};

}  // namespace

// Called from etnaviv_cl_test_gc7000.c
void etna_set_state(struct etna_cmd_stream* stream, uint32_t address, uint32_t value) {
  static_cast<MagmaExecuteMsdVsi::EtnaCommandStream*>(stream)->EtnaSetState(address, value);
}

void etna_set_state_from_bo(struct etna_cmd_stream* stream, uint32_t address, struct etna_bo* bo,
                            uint32_t reloc_flags) {
  static_cast<MagmaExecuteMsdVsi::EtnaCommandStream*>(stream)->EtnaSetStateFromBuffer(
      address, *static_cast<MagmaExecuteMsdVsi::EtnaBuffer*>(bo), reloc_flags);
}

void etna_stall(struct etna_cmd_stream* stream, uint32_t from, uint32_t to) {
  static_cast<MagmaExecuteMsdVsi::EtnaCommandStream*>(stream)->EtnaStall(from, to);
}

struct etna_bo* etna_bo_new(void* dev, uint32_t size, uint32_t flags) {
  return nullptr;
}
void* etna_bo_map(struct etna_bo* bo) { return nullptr; }
void etna_cmd_stream_finish(struct etna_cmd_stream* stream) {}
struct drm_test_info* drm_test_setup(int argc, char** argv) {
  return nullptr;
}
void drm_test_teardown(struct drm_test_info* info) {}

TEST_F(MagmaExecuteMsdVsi, ExecuteCommand) { Test(); }

TEST_F(MagmaExecuteMsdVsi, ExecuteMany) {
  for (uint32_t iter = 0; iter < 100; iter++) {
    Test();
    TearDown();
    SetUp();
  }
}

TEST_F(MagmaExecuteMsdVsi, MmuExceptionRecovery) {
  TestExecuteMmuException();
  TearDown();
  // Verify new commands complete successfully.
  SetUp();
  Test();
}

TEST_F(MagmaExecuteMsdVsi, HangRecovery) {
  TestHang();
  TearDown();
  // Verify new commands complete successfully.
  SetUp();
  Test();
}
