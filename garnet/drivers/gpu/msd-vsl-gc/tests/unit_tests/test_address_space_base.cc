// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/address_space_base.h"
#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"

class TestAddressSpaceBase {
 public:
  class TestAddressSpace : public AddressSpaceBase {
   public:
    bool Insert(gpu_addr_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                uint64_t page_count) override {
      return true;
    }

    bool Clear(gpu_addr_t addr, uint64_t page_count) override { return true; }
  };

  static void AddMapping() {
    MockBusMapper mock_bus_mapper;
    auto address_space = std::make_shared<TestAddressSpace>();
    auto buffer = std::shared_ptr<MsdVslBuffer>(MsdVslBuffer::Create(PAGE_SIZE, "Test"));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x1000)));
    EXPECT_EQ(2u, buffer.use_count());
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x2000)));
    EXPECT_EQ(3u, buffer.use_count());
  }

  static void OverlappedMapping() {
    constexpr uint32_t kPageCount = 2;
    MockBusMapper mock_bus_mapper;
    auto address_space = std::make_shared<TestAddressSpace>();
    auto buffer =
        std::shared_ptr<MsdVslBuffer>(MsdVslBuffer::Create(PAGE_SIZE * kPageCount, "Test"));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer,
        mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, kPageCount), 0x1000)));
    EXPECT_FALSE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer,
        mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, kPageCount), 0x0000)));
    EXPECT_FALSE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer,
        mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, kPageCount), 0x1000)));
    EXPECT_FALSE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer,
        mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, kPageCount), 0x2000)));
  }

  static void RemoveMapping() {
    MockBusMapper mock_bus_mapper;
    auto address_space = std::make_shared<TestAddressSpace>();
    auto buffer = std::shared_ptr<MsdVslBuffer>(MsdVslBuffer::Create(PAGE_SIZE, "Test"));
    EXPECT_FALSE(address_space->RemoveMapping(buffer->platform_buffer(), 0x1000));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x1000)));
    EXPECT_EQ(2u, buffer.use_count());
    EXPECT_TRUE(address_space->RemoveMapping(buffer->platform_buffer(), 0x1000));
    EXPECT_EQ(1u, buffer.use_count());
  }

  static void ReleaseBuffer() {
    MockBusMapper mock_bus_mapper;
    auto address_space = std::make_shared<TestAddressSpace>();
    auto buffer = std::shared_ptr<MsdVslBuffer>(MsdVslBuffer::Create(PAGE_SIZE, "Test"));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x1000)));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x2000)));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x10000)));
    EXPECT_EQ(4u, buffer.use_count());
    uint32_t removed_count;
    address_space->ReleaseBuffer(buffer->platform_buffer(), &removed_count);
    EXPECT_EQ(3u, removed_count);
    EXPECT_EQ(1u, buffer.use_count());
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x1000)));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x2000)));
    EXPECT_TRUE(address_space->AddMapping(std::make_unique<GpuMapping>(
        address_space, buffer, mock_bus_mapper.MapPageRangeBus(buffer->platform_buffer(), 0, 1),
        0x10000)));
  }
};

TEST(AddressSpaceBaseBase, AddMapping) { TestAddressSpaceBase::AddMapping(); }

TEST(AddressSpaceBaseBase, OverlappedMapping) { TestAddressSpaceBase::OverlappedMapping(); }

TEST(AddressSpaceBaseBase, RemoveMapping) { TestAddressSpaceBase::RemoveMapping(); }

TEST(AddressSpaceBaseBase, ReleaseBuffer) { TestAddressSpaceBase::ReleaseBuffer(); }
