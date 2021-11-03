// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-message.h"

#include <fidl/fuchsia.hardware.tee/cpp/wire.h>

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

  static fpromise::result<MockMessage, zx_status_t> TryCreate(
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
      return fpromise::error(status);
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
      return fpromise::error(status);
    }

    return fpromise::ok(std::move(message));
  }
};

// Fill a ParameterSet with a particular pattern of values.
static fidl::VectorView<fuchsia_tee::wire::Parameter> CreateParameters(fidl::AnyArena& allocator,
                                                                       size_t num_params) {
  uint8_t byte_val = 0;
  auto inc = [&byte_val]() { return byte_val++; };

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameters(allocator, num_params);

  for (size_t i = 0; i < num_params; i++) {
    fuchsia_tee::wire::Value value(allocator);
    value.set_direction(fuchsia_tee::wire::Direction::kInout);

    uint64_t a, b, c;

    auto a_ptr = reinterpret_cast<uint8_t*>(&a);
    auto b_ptr = reinterpret_cast<uint8_t*>(&b);
    auto c_ptr = reinterpret_cast<uint8_t*>(&c);

    std::generate(a_ptr, a_ptr + sizeof(a), inc);
    std::generate(b_ptr, b_ptr + sizeof(b), inc);
    std::generate(c_ptr, c_ptr + sizeof(c), inc);

    value.set_a(allocator, a);
    value.set_b(allocator, b);
    value.set_c(allocator, c);

    parameters[i].set_value(allocator, std::move(value));
  }
  return parameters;
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
  fidl::Arena allocator;
  constexpr size_t kParameterSetSize = 4;

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameters_in =
      CreateParameters(allocator, kParameterSetSize);

  auto result = MockMessage::TryCreate(dpool_.get(), cpool_.get(), 0, std::move(parameters_in));
  ASSERT_TRUE(result.is_ok(), "Creating a MockMessage with has failed with error %d\n",
              result.error());

  parameters_in = CreateParameters(allocator, kParameterSetSize);

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameters_out;
  result.take_value().CreateOutputParameterSet(allocator, 0, &parameters_out);

  ASSERT_EQ(parameters_in.count(), parameters_out.count());

  for (size_t i = 0; i < parameters_in.count(); i++) {
    auto& param_in = parameters_in[i];
    auto& param_out = parameters_out[i];

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
