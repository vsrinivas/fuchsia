// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "magma_vendor_queries.h"
#include "src/graphics/drivers/msd-vsi-vip/src/address_space.h"
#include "src/graphics/drivers/msd-vsi-vip/src/address_space_layout.h"
#include "src/graphics/drivers/msd-vsi-vip/src/instructions.h"
#include "src/graphics/drivers/msd-vsi-vip/src/msd_vsi_device.h"

// These tests are unit testing the functionality of MsdVsiDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class MsdVsiDeviceTest : public ::testing::Test {
 public:
  void SetUp() override {
    device_ = MsdVsiDevice::Create(GetTestDeviceHandle(), false /* start_device_thread */);
    EXPECT_NE(device_, nullptr);
  }

 protected:
  std::unique_ptr<MsdVsiDevice> device_;  // Device should be destroyed last.
};

TEST_F(MsdVsiDeviceTest, CreateAndDestroy) {}

TEST_F(MsdVsiDeviceTest, Shutdown) {
  device_->StartDeviceThread();
  EXPECT_TRUE(device_->Shutdown());
}

TEST_F(MsdVsiDeviceTest, DeviceId) {
  EXPECT_TRUE((device_->device_id() == 0x7000u) || (device_->device_id() == 0x8000u));
}

TEST_F(MsdVsiDeviceTest, ChipIdentity) {
  magma_vsi_vip_chip_identity identity;
  ASSERT_EQ(MAGMA_STATUS_OK, device_->ChipIdentity(&identity));
  EXPECT_GT(identity.chip_model, 0u);
  EXPECT_GT(identity.chip_revision, 0u);
  EXPECT_GT(identity.chip_date, 0u);
  EXPECT_GT(identity.product_id, 0u);

  // Now try to get it as a buffer.
  uint32_t identity_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK, msd_device_query_returns_buffer(
                                 device_.get(), kMsdVsiVendorQueryChipIdentity, &identity_buffer));
  magma_vsi_vip_chip_identity identity_from_buf;
  auto buffer = magma::PlatformBuffer::Import(identity_buffer);
  EXPECT_TRUE(buffer);
  EXPECT_TRUE(buffer->Read(&identity_from_buf, 0, sizeof(identity_from_buf)));

  EXPECT_EQ(0, memcmp(&identity, &identity_from_buf, sizeof(identity_from_buf)));
}

TEST_F(MsdVsiDeviceTest, QueryReturnsBufferBadId) {
  uint32_t buffer;
  EXPECT_NE(MAGMA_STATUS_OK, msd_device_query_returns_buffer(device_.get(), 0 /* id */, &buffer));
}

TEST_F(MsdVsiDeviceTest, ChipOption) {
  magma_vsi_vip_chip_option option;
  ASSERT_EQ(MAGMA_STATUS_OK, device_->ChipOption(&option));

  // Now try to get it as a buffer.
  uint32_t option_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK, msd_device_query_returns_buffer(
                                 device_.get(), kMsdVsiVendorQueryChipOption, &option_buffer));
  magma_vsi_vip_chip_option option_from_buf;
  auto buffer = magma::PlatformBuffer::Import(option_buffer);
  EXPECT_TRUE(buffer);
  EXPECT_TRUE(buffer->Read(&option_from_buf, 0, sizeof(option_from_buf)));

  EXPECT_EQ(0, memcmp(&option, &option_from_buf, sizeof(option_from_buf)));
}

TEST_F(MsdVsiDeviceTest, QuerySram) {
  uint32_t sram_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK, msd_device_query_returns_buffer(
                                 device_.get(), kMsdVsiVendorQueryExternalSram, &sram_buffer));

  auto buffer = magma::PlatformBuffer::Import(sram_buffer);
  ASSERT_TRUE(buffer);
}

TEST_F(MsdVsiDeviceTest, FetchEngineDma) {
  constexpr uint32_t kPageCount = 1;

  EXPECT_TRUE(device_->IsIdle());

  std::unique_ptr<magma::PlatformBuffer> buffer =
      magma::PlatformBuffer::Create(PAGE_SIZE * kPageCount, "test");
  ASSERT_NE(buffer, nullptr);

  auto bus_mapping = device_->GetBusMapper()->MapPageRangeBus(buffer.get(), 0, kPageCount);
  ASSERT_NE(bus_mapping, nullptr);

  uint32_t length = 0;
  {
    uint32_t* cmd_ptr;
    ASSERT_TRUE(buffer->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));

    cmd_ptr[length++] = (2 << 27);  // end

    EXPECT_TRUE(buffer->UnmapCpu());
    EXPECT_TRUE(buffer->CleanCache(0, PAGE_SIZE * kPageCount, false));
  }

  length *= sizeof(uint32_t);
  uint16_t prefetch = 0;

  EXPECT_TRUE(device_->SubmitCommandBufferNoMmu(bus_mapping->Get()[0], length, &prefetch));
  EXPECT_EQ(magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t), prefetch);

  auto start = std::chrono::high_resolution_clock::now();
  while (!device_->IsIdle() && std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::high_resolution_clock::now() - start)
                                       .count() < 1000) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(device_->IsIdle());

  auto dma_addr = registers::DmaAddress::Get().ReadFrom(device_->register_io());
  EXPECT_EQ(dma_addr.reg_value(), bus_mapping->Get()[0] + prefetch * sizeof(uint64_t));
}

TEST_F(MsdVsiDeviceTest, LoadAddressSpace) {
  class AddressSpaceOwner : public AddressSpace::Owner {
   public:
    AddressSpaceOwner(magma::PlatformBusMapper* bus_mapper) : bus_mapper_(bus_mapper) {}

    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_; }

    void AddressSpaceReleased(AddressSpace* address_space) override {}

   private:
    magma::PlatformBusMapper* bus_mapper_;
  };

  // Make sure the automatically created device is destructed, so that registering
  // interrupts does not fail.
  device_ = nullptr;

  // Ensure we can do this > once
  for (uint32_t i = 0; i < 2; i++) {
    std::unique_ptr<MsdVsiDevice> device =
        MsdVsiDevice::Create(GetTestDeviceHandle(), false /* start_device_thread */);
    ASSERT_NE(device, nullptr);

    EXPECT_TRUE(device->IsIdle());

    AddressSpaceOwner owner(device->GetBusMapper());

    static constexpr uint32_t kAddressSpaceIndex = 1;

    std::unique_ptr<AddressSpace> address_space = AddressSpace::Create(&owner, kAddressSpaceIndex);
    ASSERT_NE(device, nullptr);

    device->page_table_arrays()->AssignAddressSpace(kAddressSpaceIndex, address_space.get());

    // Switch to the address space with a command buffer.

    static constexpr uint32_t kPageCount = 1;

    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(PAGE_SIZE * kPageCount, "test");
    ASSERT_NE(buffer, nullptr);

    auto bus_mapping = device->GetBusMapper()->MapPageRangeBus(buffer.get(), 0, kPageCount);
    ASSERT_NE(bus_mapping, nullptr);

    uint32_t length = 0;
    {
      uint32_t* cmd_ptr;
      ASSERT_TRUE(buffer->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));

      cmd_ptr[length++] =
          (1 << 27)                                                   // load state
          | (1 << 16)                                                 // count
          | (registers::MmuPageTableArrayConfig::Get().addr() >> 2);  // register to be written
      cmd_ptr[length++] = kAddressSpaceIndex;
      cmd_ptr[length++] = (2 << 27);  // end

      EXPECT_TRUE(buffer->UnmapCpu());
      EXPECT_TRUE(buffer->CleanCache(0, PAGE_SIZE * kPageCount, false));
    }

    length *= sizeof(uint32_t);
    uint16_t prefetch = 0;

    EXPECT_TRUE(device->SubmitCommandBufferNoMmu(bus_mapping->Get()[0], length, &prefetch));
    EXPECT_EQ(magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t), prefetch);

    auto start = std::chrono::high_resolution_clock::now();
    while (!device->IsIdle() && std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::high_resolution_clock::now() - start)
                                        .count() < 1000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(device->IsIdle());

    auto dma_addr = registers::DmaAddress::Get().ReadFrom(device->register_io());
    EXPECT_EQ(dma_addr.reg_value(), bus_mapping->Get()[0] + prefetch * sizeof(uint64_t));

    device->page_table_arrays()->Enable(device->register_io(), true);
  }
}

TEST_F(MsdVsiDeviceTest, Connections) {
  std::vector<std::unique_ptr<MsdVsiConnection>> connections;
  for (uint32_t i = 0; i < PageTableArrays::size(); i++) {
    auto connection = device_->Open(i);
    EXPECT_NE(nullptr, connection);
    EXPECT_EQ(connection->client_id(), i);
    connections.push_back(std::move(connection));
  }
  // Reached the limit
  auto connection = device_->Open(0);
  EXPECT_EQ(nullptr, connection);
  connections.clear();
  // Ok to create more now
  connection = device_->Open(0);
  EXPECT_NE(nullptr, connection);
}

TEST_F(MsdVsiDeviceTest, RingbufferCanHoldMaxEvents) {
  // The ringbuffer starts off with a WAIT-LINK instruction, so subtract this from the total space.
  uint32_t wait_link_size = 2 * kInstructionDwords * sizeof(uint32_t);
  uint32_t available_space = AddressSpaceLayout::ringbuffer_size() - wait_link_size;
  uint32_t max_used_space =
      MsdVsiDevice::kRbMaxInstructionsPerEvent * sizeof(uint64_t) * MsdVsiDevice::kNumEvents;
  ASSERT_GE(available_space, max_used_space);
}

TEST_F(MsdVsiDeviceTest, PulseEater) {
  uint32_t pulse_eater = device_->register_io()->Read32(0x10C);
  EXPECT_TRUE(pulse_eater & (1 << 18)) << "missing performance fix";
}
