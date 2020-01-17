// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_device.h"
#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"

class TestEvents : public ::testing::Test {
 public:
  static constexpr uint32_t kAddressSpaceIndex = 1;

  void SetUp() override {
    device_ = MsdVslDevice::Create(GetTestDeviceHandle());
    EXPECT_NE(device_, nullptr);

    address_space_owner_ =
        std::make_unique<AddressSpaceOwner>(device_->GetBusMapper());
    address_space_ = AddressSpace::Create(address_space_owner_.get());
    EXPECT_NE(address_space_, nullptr);
    device_->page_table_arrays()->AssignAddressSpace(kAddressSpaceIndex,
                                                     address_space_.get());
  }

 protected:
  class AddressSpaceOwner : public AddressSpace::Owner {
   public:
    AddressSpaceOwner(magma::PlatformBusMapper* bus_mapper) : bus_mapper_(bus_mapper) {}
    virtual ~AddressSpaceOwner() = default;

    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_; }

   private:
    magma::PlatformBusMapper* bus_mapper_;
  };

  void StopRingbuffer() {
    device_->StopRingbuffer();

    auto start = std::chrono::high_resolution_clock::now();
    while (!device_->IsIdle() &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::high_resolution_clock::now() - start)
                  .count() < 1000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto reg = registers::IdleState::Get().ReadFrom(device_->register_io());
    EXPECT_EQ(0x7FFFFFFFu, reg.reg_value());
  }

  std::unique_ptr<AddressSpaceOwner> address_space_owner_;
  std::shared_ptr<AddressSpace> address_space_;
  std::unique_ptr<MsdVslDevice> device_;
};

TEST_F(TestEvents, AllocAndFree) {
  for (unsigned int i = 0; i < 2; i++) {
    uint32_t event_ids[MsdVslDevice::kNumEvents] = {};
    for (unsigned int j = 0; j < MsdVslDevice::kNumEvents; j++) {
      ASSERT_TRUE(device_->AllocInterruptEvent(&event_ids[j]));
    }
    // We should have no events left.
    uint32_t id;
    ASSERT_FALSE(device_->AllocInterruptEvent(&id));

    ASSERT_FALSE(device_->CompleteInterruptEvent(0));  // Not yet submitted.

    for (unsigned int j = 0; j < MsdVslDevice::kNumEvents; j++) {
      ASSERT_TRUE(device_->FreeInterruptEvent(event_ids[j]));
    }
    ASSERT_FALSE(device_->FreeInterruptEvent(0));  // Already freed.
    ASSERT_FALSE(device_->FreeInterruptEvent(100));  // Out of bounds.
  }
  ASSERT_FALSE(device_->CompleteInterruptEvent(0));  // Not yet allocated.
}

TEST_F(TestEvents, Write) {
  // We need to load the address space as we are writing to the ringbuffer directly,
  // rather than via SubmitCommandBuffer.
  ASSERT_TRUE(device_->LoadInitialAddressSpace(address_space_, kAddressSpaceIndex));
  ASSERT_TRUE(device_->StartRingbuffer(address_space_));

  auto& ringbuffer = device_->ringbuffer_;
  uint64_t rb_gpu_addr;
  ASSERT_TRUE(ringbuffer->GetGpuAddress(&rb_gpu_addr));

  // Allocate the maximum number of interrupt events, and corresponding semaphores.
  uint32_t event_ids[MsdVslDevice::kNumEvents] = {};
  std::unique_ptr<magma::PlatformSemaphore> semaphores[MsdVslDevice::kNumEvents] = {};
  for (unsigned int i = 0; i < MsdVslDevice::kNumEvents; i++) {
    ASSERT_TRUE(device_->AllocInterruptEvent(&event_ids[i]));
    auto semaphore = magma::PlatformSemaphore::Create();
    ASSERT_NE(semaphore, nullptr);
    semaphores[i] = std::move(semaphore);
  }

  for (unsigned int i = 0; i < 2; i++) {
    // We will link to the end of the ringbuffer, where we are adding new events.
    uint32_t rb_link_addr = rb_gpu_addr + ringbuffer->tail();

    for (unsigned int j = 0; j < MsdVslDevice::kNumEvents; j++) {
      auto copy = semaphores[j]->Clone();
      ASSERT_NE(copy, nullptr);
      ASSERT_TRUE(device_->WriteInterruptEvent(event_ids[j], std::move(copy)));
      // Should not be able to submit the same event while it is still pending.
      ASSERT_FALSE(device_->WriteInterruptEvent(event_ids[j], nullptr));
    }

    ASSERT_TRUE(device_->AddRingbufferWaitLink());

    // Link the ringbuffer to the newly written events.
    uint32_t num_new_rb_instructions = MsdVslDevice::kNumEvents + 2;  // Add 2 for WAIT-LINK.
    device_->LinkRingbuffer(num_new_rb_instructions, rb_link_addr,
                            num_new_rb_instructions /* prefetch */);

    constexpr uint64_t kTimeoutMs = 5000;
    for (unsigned int j = 0; j < MsdVslDevice::kNumEvents; j++) {
      EXPECT_EQ(MAGMA_STATUS_OK, semaphores[j]->Wait(kTimeoutMs).get());
    }
  }

  for (unsigned int i = 0; i < MsdVslDevice::kNumEvents; i++) {
    ASSERT_TRUE(device_->FreeInterruptEvent(event_ids[i]));
  }

  StopRingbuffer();
}

TEST_F(TestEvents, Submit) {
  for (int i = 0; i < 10; i++) {
    uint32_t event_id;
    ASSERT_TRUE(device_->AllocInterruptEvent(&event_id));
    auto semaphore = magma::PlatformSemaphore::Create();
    ASSERT_NE(semaphore, nullptr);

    uint16_t prefetch_out;
    ASSERT_TRUE(device_->SubmitCommandBuffer(address_space_, kAddressSpaceIndex,
                                             nullptr /* buf */, 0 /* gpu_addr */, 0 /* length */,
                                             event_id, semaphore->Clone(), &prefetch_out));

    constexpr uint64_t kTimeoutMs = 1000;
    EXPECT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());

    ASSERT_TRUE(device_->FreeInterruptEvent(event_id));
  }

  {
    // The ringbuffer should be in WAIT-LINK until we explicitly stop it.
    auto reg = registers::IdleState::Get().ReadFrom(device_->register_io());
    EXPECT_NE(0x7FFFFFFFu, reg.reg_value());
  }

  StopRingbuffer();
}
