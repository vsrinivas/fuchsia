// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-message.h"

#include <fuchsia/hardware/tee/llcpp/fidl.h>

#include <memory>
#include <numeric>

#include <zxtest/zxtest.h>

#include "optee-llcpp.h"
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
      fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set) {
    ZX_DEBUG_ASSERT(message_pool != nullptr);
    ZX_DEBUG_ASSERT(temp_memory_pool != nullptr);

    const size_t num_params = parameter_set.count() + start_index;
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
    status =
        message.TryInitializeParameters(start_index, std::move(parameter_set), temp_memory_pool);
    if (status != ZX_OK) {
      return fit::error(status);
    }

    return fit::ok(std::move(message));
  }
};

// Fill a ParameterSet with a particular pattern of values.
static ParameterSet CreateParameterSet(size_t num_params) {
  uint8_t byte_val = 0;
  auto inc = [&byte_val]() { return byte_val++; };

  std::vector<Parameter> parameters;
  parameters.reserve(num_params);

  for (size_t i = 0; i < num_params; i++) {
    Value value;
    value.set_direction(fuchsia_tee::wire::Direction::INOUT);

    uint64_t a, b, c;

    auto a_ptr = reinterpret_cast<uint8_t*>(&a);
    auto b_ptr = reinterpret_cast<uint8_t*>(&b);
    auto c_ptr = reinterpret_cast<uint8_t*>(&c);

    std::generate(a_ptr, a_ptr + sizeof(a), inc);
    std::generate(b_ptr, b_ptr + sizeof(b), inc);
    std::generate(c_ptr, c_ptr + sizeof(c), inc);

    value.set_a(a);
    value.set_b(b);
    value.set_c(c);

    Parameter param;
    param.set_value(std::move(value));

    parameters.push_back(std::move(param));
  }

  ParameterSet parameter_set;
  parameter_set.set_parameters(std::move(parameters));

  return parameter_set;
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

// Tests that the ParameterSet can be converted to a message and back.
TEST_F(MessageTest, ParameterSetInvertabilityTest) {
  constexpr size_t kParameterSetSize = 4;

  ParameterSet parameter_set_in = CreateParameterSet(kParameterSetSize);
  ParameterSet parameter_set_out{};

  fidl::VectorView<fuchsia_tee::wire::Parameter> llcpp_parameter_set_in =
      parameter_set_in.to_llcpp();

  auto result =
      MockMessage::TryCreate(dpool_.get(), cpool_.get(), 0, std::move(llcpp_parameter_set_in));
  ASSERT_TRUE(result.is_ok(), "Creating a MockMessage with has failed with error %d\n",
              result.error());

  result.take_value().CreateOutputParameterSet(0, &parameter_set_out);

  fidl::VectorView<fuchsia_tee::wire::Parameter> llcpp_parameter_set_out =
      parameter_set_out.to_llcpp();

  llcpp_parameter_set_in = parameter_set_in.to_llcpp();
  ASSERT_EQ(llcpp_parameter_set_in.count(), llcpp_parameter_set_out.count());

  for (size_t i = 0; i < llcpp_parameter_set_in.count(); i++) {
    auto& param_in = llcpp_parameter_set_in[i];
    auto& param_out = llcpp_parameter_set_out[i];

    ASSERT_TRUE(param_in.is_value());
    ASSERT_TRUE(param_out.is_value());

    auto& value_in = param_in.value();
    auto& value_out = param_out.value();

    ASSERT_TRUE(value_in.has_a());
    ASSERT_TRUE(value_out.has_a());
    ASSERT_TRUE(value_in.has_b());
    ASSERT_TRUE(value_out.has_b());
    ASSERT_TRUE(value_in.has_c());
    ASSERT_TRUE(value_out.has_c());

    ASSERT_EQ(value_in.a(), value_out.a());
    ASSERT_EQ(value_in.b(), value_out.b());
    ASSERT_EQ(value_in.c(), value_out.c());
  }
}

}  // namespace

}  // namespace optee
