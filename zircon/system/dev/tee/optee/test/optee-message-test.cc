// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-message.h"

#include <fuchsia/hardware/tee/c/fidl.h>

#include <memory>
#include <numeric>

#include <zxtest/zxtest.h>

#include "shared-memory.h"

namespace optee {

namespace {

class MockMessage : public Message {
 public:
  using Message::CalculateSize;
  using Message::CreateOutputParameterSet;
  using Message::Message;

  static fit::result<MockMessage, zx_status_t> TryCreate(
      SharedMemoryManager::DriverMemoryPool* message_pool,
      SharedMemoryManager::ClientMemoryPool* temp_memory_pool, size_t start_index,
      const fuchsia_tee_ParameterSet& parameter_set) {
    ZX_DEBUG_ASSERT(message_pool != nullptr);
    ZX_DEBUG_ASSERT(temp_memory_pool != nullptr);

    const size_t num_params = parameter_set.count + start_index;
    ZX_DEBUG_ASSERT(num_params <= std::numeric_limits<uint32_t>::max());

    SharedMemoryPtr memory;
    zx_status_t status = message_pool->Allocate(CalculateSize(num_params), &memory);
    if (status != ZX_OK) {
      return fit::error(status);
    }

    MockMessage message(std::move(memory));

    // Don't care about the specific values in the header except the num_params.
    message.header()->command = 0;
    message.header()->cancel_id = 0;
    message.header()->num_params = static_cast<uint32_t>(num_params);

    // Don't care about the values in the fixed parameters before start_index

    // Initialize the message params (starting from start_index) with the ParameterSet
    status = message.TryInitializeParameters(start_index, parameter_set, temp_memory_pool);
    if (status != ZX_OK) {
      return fit::error(status);
    }

    return fit::ok(std::move(message));
  }
};

// Fill a ParameterSet with a particular pattern of values.
static void InitializeParameterSet(fuchsia_tee_ParameterSet* parameter_set) {
  parameter_set->count = 4;
  uint8_t byte_val = 0;
  auto inc = [&byte_val]() { return byte_val++; };

  for (size_t i = 0; i < parameter_set->count; i++) {
    parameter_set->parameters[i].tag = fuchsia_tee_ParameterTag_value;
    parameter_set->parameters[i].value.direction = fuchsia_tee_Direction_INOUT;

    auto a = reinterpret_cast<uint8_t*>(&parameter_set->parameters[i].value.a);
    auto b = reinterpret_cast<uint8_t*>(&parameter_set->parameters[i].value.b);
    auto c = reinterpret_cast<uint8_t*>(&parameter_set->parameters[i].value.c);
    auto asz = sizeof(parameter_set->parameters[i].value.a);
    auto bsz = sizeof(parameter_set->parameters[i].value.b);
    auto csz = sizeof(parameter_set->parameters[i].value.c);

    std::generate(a, a + asz, inc);
    std::generate(b, b + bsz, inc);
    std::generate(c, c + csz, inc);
  }
}

class MessageTest : public zxtest::Test {
  void SetUp() {
    addr_ = std::make_unique<uint8_t[]>(pool_size_ * 2);
    auto vaddr = reinterpret_cast<zx_vaddr_t>(addr_.get());
    auto paddr = reinterpret_cast<zx_paddr_t>(addr_.get());

    dpool_ = std::unique_ptr<SharedMemoryManager::DriverMemoryPool>(
        new SharedMemoryManager::DriverMemoryPool(paddr, vaddr, pool_size_));
    cpool_ = std::unique_ptr<SharedMemoryManager::ClientMemoryPool>(
        new SharedMemoryManager::ClientMemoryPool(vaddr + pool_size_, paddr + pool_size_,
                                                  pool_size_));
  }

  static constexpr size_t pool_size_ = PAGE_SIZE * 2;
  std::unique_ptr<uint8_t[]> addr_;

 public:
  std::unique_ptr<SharedMemoryManager::DriverMemoryPool> dpool_;
  std::unique_ptr<SharedMemoryManager::ClientMemoryPool> cpool_;
};

// Tests that independent of the starting index in the particular Message variant,
// the ParameterSet can be converted to a message and back.
TEST_F(MessageTest, ParameterSetInvertabilityTest) {
  fuchsia_tee_ParameterSet parameter_set_in = {};
  fuchsia_tee_ParameterSet parameter_set_out = {};

  memset(&parameter_set_in, 0, sizeof(parameter_set_in));
  memset(&parameter_set_out, 0, sizeof(parameter_set_out));

  InitializeParameterSet(&parameter_set_in);

  for (size_t starting_index = 0; starting_index < 4; starting_index++) {
    auto result =
        MockMessage::TryCreate(dpool_.get(), cpool_.get(), starting_index, parameter_set_in);
    ASSERT_TRUE(result.is_ok(),
                "Creating a MockMessage with starting_index=%zu has failed with error %d\n",
                starting_index, result.error());
    result.take_value().CreateOutputParameterSet(starting_index, &parameter_set_out);
    ASSERT_EQ(0, memcmp(&parameter_set_in, &parameter_set_out, sizeof(fuchsia_tee_ParameterSet)));
  }
}

}  // namespace

}  // namespace optee
