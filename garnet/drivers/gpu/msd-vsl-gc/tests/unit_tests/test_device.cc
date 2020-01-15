// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include "garnet/drivers/gpu/msd-vsl-gc/src/address_space.h"
#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_device.h"
#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "magma_vendor_queries.h"

// These tests are unit testing the functionality of MsdVslDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class MsdVslDeviceTest : public ::testing::Test {
 public:
  void SetUp() override {
    device_ = MsdVslDevice::Create(GetTestDeviceHandle());
    EXPECT_NE(device_, nullptr);
  }

  std::unique_ptr<MsdVslDevice> device_;
};

TEST_F(MsdVslDeviceTest, CreateAndDestroy) {}

TEST_F(MsdVslDeviceTest, DeviceId) {
  EXPECT_TRUE((device_->device_id() == 0x7000u) || (device_->device_id() == 0x8000u));
}

TEST_F(MsdVslDeviceTest, ChipIdentity) {
  magma_vsl_gc_chip_identity identity;
  ASSERT_EQ(MAGMA_STATUS_OK, device_->ChipIdentity(&identity));
  EXPECT_GT(identity.chip_model, 0u);
  EXPECT_GT(identity.chip_revision, 0u);
  EXPECT_GT(identity.chip_date, 0u);
  EXPECT_GT(identity.product_id, 0u);

  // Now try to get it as a buffer.
  uint32_t identity_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK, msd_device_query_returns_buffer(
                                 device_.get(), kMsdVslVendorQueryChipIdentity, &identity_buffer));
  magma_vsl_gc_chip_identity identity_from_buf;
  auto buffer = magma::PlatformBuffer::Import(identity_buffer);
  EXPECT_TRUE(buffer);
  EXPECT_TRUE(buffer->Read(&identity_from_buf, 0, sizeof(identity_from_buf)));

  EXPECT_EQ(0, memcmp(&identity, &identity_from_buf, sizeof(identity_from_buf)));
}

TEST_F(MsdVslDeviceTest, QueryReturnsBufferBadId) {
  uint32_t buffer;
  EXPECT_NE(MAGMA_STATUS_OK, msd_device_query_returns_buffer(device_.get(), 0 /* id */, &buffer));
}

TEST_F(MsdVslDeviceTest, ChipOption) {
  magma_vsl_gc_chip_option option;
  ASSERT_EQ(MAGMA_STATUS_OK, device_->ChipOption(&option));

  // Now try to get it as a buffer.
  uint32_t option_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK, msd_device_query_returns_buffer(
                                 device_.get(), kMsdVslVendorQueryChipOption, &option_buffer));
  magma_vsl_gc_chip_option option_from_buf;
  auto buffer = magma::PlatformBuffer::Import(option_buffer);
  EXPECT_TRUE(buffer);
  EXPECT_TRUE(buffer->Read(&option_from_buf, 0, sizeof(option_from_buf)));

  EXPECT_EQ(0, memcmp(&option, &option_from_buf, sizeof(option_from_buf)));
}

TEST_F(MsdVslDeviceTest, FetchEngineDma) {
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

TEST_F(MsdVslDeviceTest, LoadAddressSpace) {
  class AddressSpaceOwner : public AddressSpace::Owner {
   public:
    AddressSpaceOwner(magma::PlatformBusMapper* bus_mapper) : bus_mapper_(bus_mapper) {}

    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_; }

   private:
    magma::PlatformBusMapper* bus_mapper_;
  };

  // Make sure the automatically created device is destructed, so that registering
  // interrupts does not fail.
  device_ = nullptr;

  // Ensure we can do this > once
  for (uint32_t i = 0; i < 2; i++) {
    std::unique_ptr<MsdVslDevice> device = MsdVslDevice::Create(GetTestDeviceHandle());
    ASSERT_NE(device, nullptr);

    EXPECT_TRUE(device->IsIdle());

    AddressSpaceOwner owner(device->GetBusMapper());

    std::unique_ptr<AddressSpace> address_space = AddressSpace::Create(&owner);
    ASSERT_NE(device, nullptr);

    static constexpr uint32_t kAddressSpaceIndex = 1;

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

TEST_F(MsdVslDeviceTest, Connections) {
  std::vector<std::unique_ptr<MsdVslConnection>> connections;
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
