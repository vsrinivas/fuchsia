// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "helper/inflight_list.h"
#include "helper/test_device_helper.h"
#include "magma.h"
#include "msd_intel_gen_query.h"

namespace {

class TestConnection : public magma::TestDeviceBase {
 public:
  TestConnection() : magma::TestDeviceBase(MAGMA_VENDOR_ID_INTEL) {
    magma_create_connection2(device(), &connection_);

    magma_status_t status =
        magma_query2(device(), kMsdIntelGenQueryExtraPageCount, &extra_page_count_);
    if (status != MAGMA_STATUS_OK) {
      fprintf(stderr, "Failed to query kMsdIntelGenQueryExtraPageCount: %d\n", status);
      extra_page_count_ = 0;
    }
  }

  ~TestConnection() {
    if (connection_)
      magma_release_connection(connection_);
  }

  void CheckWorkarounds(uint32_t register_offset, uint32_t expected_value) {
    ASSERT_TRUE(connection_);

    uint32_t context_id;
    magma_create_context(connection_, &context_id);

    magma_status_t status = magma_get_error(connection_);
    ASSERT_EQ(MAGMA_STATUS_OK, status);

    uint64_t size;
    magma_buffer_t batch_buffer;
    magma_buffer_t result_buffer;

    status = magma_create_buffer(connection_, PAGE_SIZE, &size, &batch_buffer);
    ASSERT_EQ(MAGMA_STATUS_OK, status);

    status = magma_create_buffer(connection_, PAGE_SIZE, &size, &result_buffer);
    ASSERT_EQ(MAGMA_STATUS_OK, status);

    magma_map_buffer_gpu(connection_, batch_buffer, 0, 1, gpu_addr_, 0);
    gpu_addr_ += (1 + extra_page_count_) * PAGE_SIZE;

    magma_map_buffer_gpu(connection_, result_buffer, 0, 1, gpu_addr_, 0);

    EXPECT_TRUE(InitBatchBuffer(batch_buffer, register_offset, gpu_addr_));

    EXPECT_TRUE(ClearBuffer(result_buffer, 0xabcd1234));

    magma_system_command_buffer command_buffer;
    std::vector<magma_system_exec_resource> exec_resources;
    EXPECT_TRUE(InitCommandBuffer(&command_buffer, &exec_resources, batch_buffer, result_buffer));

    magma_execute_command_buffer_with_resources(connection_, context_id, &command_buffer,
                                                exec_resources.data(), nullptr);

    {
      magma::InflightList list;
      static constexpr int64_t kOneSecondInNs = 1000000000;
      magma::Status status = list.WaitForCompletion(connection_, kOneSecondInNs);
      EXPECT_EQ(MAGMA_STATUS_OK, status.get());
    }

    uint32_t result;
    EXPECT_TRUE(ReadBufferAt(result_buffer, 0, &result));

    EXPECT_EQ(expected_value, result)
        << " expected: 0x" << std::hex << expected_value << " got: 0x" << result;

    magma_release_buffer(connection_, result_buffer);
    magma_release_buffer(connection_, batch_buffer);
    magma_release_context(connection_, context_id);

    status = magma_get_error(connection_);
    ASSERT_EQ(MAGMA_STATUS_OK, status);
  }

  bool ReadBufferAt(magma_buffer_t buffer, uint32_t dword_offset, uint32_t* result_out) {
    void* vaddr;
    magma_status_t status = magma_map(connection_, buffer, &vaddr);
    if (status != MAGMA_STATUS_OK)
      return DRETF(false, "magma_map failed: %d", status);

    *result_out = reinterpret_cast<uint32_t*>(vaddr)[dword_offset];

    status = magma_unmap(connection_, buffer);
    if (status != MAGMA_STATUS_OK)
      return DRETF(false, "magma_unmap failed: %d", status);

    return true;
  }

  bool ClearBuffer(magma_buffer_t buffer, uint32_t value) {
    void* vaddr;
    magma_status_t status = magma_map(connection_, buffer, &vaddr);
    if (status != MAGMA_STATUS_OK)
      return DRETF(false, "magma_map failed: %d", status);

    for (uint32_t i = 0; i < magma_get_buffer_size(buffer) / sizeof(uint32_t); i++) {
      reinterpret_cast<uint32_t*>(vaddr)[i] = value;
    }

    status = magma_unmap(connection_, buffer);
    if (status != MAGMA_STATUS_OK)
      return DRETF(false, "magma_unmap failed: %d", status);

    return true;
  }

  bool InitBatchBuffer(magma_buffer_t buffer, uint32_t register_offset, uint64_t target_gpu_addr) {
    void* vaddr;
    magma_status_t status = magma_map(connection_, buffer, &vaddr);
    if (status != MAGMA_STATUS_OK)
      return DRETF(false, "magma_map failed: %d", status);

    memset(vaddr, 0, magma_get_buffer_size(buffer));

    {
      auto batch_ptr = reinterpret_cast<uint32_t*>(vaddr);
      *batch_ptr++ = (0 << 29)       // command type: MI_COMMAND
                     | (0x24 << 23)  // command opcode: MI_STORE_REGISTER_MEM
                     | (2 << 0);     // number of dwords - 2
      *batch_ptr++ = register_offset;
      *batch_ptr++ = magma::lower_32_bits(target_gpu_addr);
      *batch_ptr++ = magma::upper_32_bits(target_gpu_addr);

      *batch_ptr++ = (0 << 29)       // command type: MI_COMMAND
                     | (0xA << 23);  // command opcode: MI_BATCH_BUFFER_END
    }

    status = magma_unmap(connection_, buffer);
    if (status != MAGMA_STATUS_OK)
      return DRETF(false, "magma_unmap failed: %d", status);

    return true;
  }

  bool InitCommandBuffer(magma_system_command_buffer* command_buffer,
                         std::vector<magma_system_exec_resource>* exec_resources,
                         magma_buffer_t batch_buffer, magma_buffer_t result_buffer) {
    exec_resources->clear();

    exec_resources->push_back({.buffer_id = magma_get_buffer_id(batch_buffer),
                               .offset = 0,
                               .length = magma_get_buffer_size(batch_buffer)});

    exec_resources->push_back({.buffer_id = magma_get_buffer_id(result_buffer),
                               .offset = 0,
                               .length = magma_get_buffer_size(result_buffer)});

    command_buffer->batch_buffer_resource_index = 0;
    command_buffer->batch_start_offset = 0;
    command_buffer->resource_count = exec_resources->size();
    command_buffer->wait_semaphore_count = 0;
    command_buffer->signal_semaphore_count = 0;

    return true;
  }

 private:
  magma_connection_t connection_;
  uint64_t extra_page_count_ = 0;
  uint64_t gpu_addr_ = 0;
};

}  // namespace

TEST(Workarounds, Register0x7004) { TestConnection().CheckWorkarounds(0x7004, 0x29c2); }

TEST(Workarounds, Register0x7300) { TestConnection().CheckWorkarounds(0x7300, 0x810); }
