// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "mock/mock_bus_mapper.h"
#include "src/graphics/drivers/msd-vsi-vip/src/address_space.h"

class TestAddressSpace : public ::testing::Test {
 public:
  void SetUp() override {
    address_space_ = AddressSpace::Create(&owner_, 0);
    ASSERT_NE(nullptr, address_space_);
  }

 protected:
  class MockAddressSpaceOwner : public AddressSpace::Owner {
   public:
    // Put bus addresses close to the 40 bit limit
    MockAddressSpaceOwner() : bus_mapper_((1ul << (40 - 1))) {}

    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

    void AddressSpaceReleased(AddressSpace* address_space) override {
      address_space_released_ = address_space;
    }

    AddressSpace* address_space_released() { return address_space_released_; }

   private:
    MockBusMapper bus_mapper_;
    AddressSpace* address_space_released_ = nullptr;
  };

  MockAddressSpaceOwner owner_;
  std::shared_ptr<AddressSpace> address_space_;

  void CheckPteEntriesClear(uint32_t gpu_addr, uint64_t page_count) {
    for (unsigned int i = 0; i < page_count; i++) {
      uint32_t addr = gpu_addr + i * PAGE_SIZE;
      uint32_t page_table_index = (addr >>= PAGE_SHIFT) & AddressSpace::kPageTableMask;
      uint32_t page_directory_index =
          (addr >>= AddressSpace::kPageTableShift) & AddressSpace::kPageDirectoryMask;

      auto page_table = address_space_->root_->GetPageTable(page_directory_index, false);
      auto* pde = address_space_->root_->entry(page_directory_index);

      if (page_table) {
        auto* pte = page_table->entry(page_table_index);
        EXPECT_EQ(*pte, AddressSpace::kInvalidPte);
        EXPECT_NE(*pde, AddressSpace::kInvalidPde);
      } else {
        EXPECT_EQ(*pde, AddressSpace::kInvalidPde);
      }
    }
  }

  void CheckPteEntries(magma::PlatformBusMapper::BusMapping* bus_mapping, uint32_t gpu_addr,
                       uint64_t mapping_page_count) {
    std::vector<uint64_t>& bus_addr = bus_mapping->Get();
    ASSERT_LE(mapping_page_count, bus_addr.size());

    for (unsigned int i = 0; i < mapping_page_count; i++) {
      uint32_t addr = gpu_addr + i * PAGE_SIZE;
      uint32_t page_table_index = (addr >>= PAGE_SHIFT) & AddressSpace::kPageTableMask;
      uint32_t page_directory_index =
          (addr >>= AddressSpace::kPageTableShift) & AddressSpace::kPageDirectoryMask;

      auto page_table = address_space_->root_->GetPageTable(page_directory_index, false);
      ASSERT_NE(page_table, nullptr);

      auto* pde = address_space_->root_->entry(page_directory_index);
      EXPECT_NE(*pde, AddressSpace::kInvalidPde);

      AddressSpace::pte_t expected_pte;
      EXPECT_TRUE(AddressSpace::pte_encode(bus_addr[i], true, true, true, &expected_pte));
      auto* pte = page_table->entry(page_table_index);
      EXPECT_EQ(*pte, expected_pte);
    }
  }

  void Insert(uint32_t gpu_addr, uint32_t size_in_pages, uint32_t mapping_page_count) {
    auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
    auto bus_mapping = owner_.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, mapping_page_count);
    EXPECT_TRUE(address_space_->Insert(gpu_addr, bus_mapping.get()));
    CheckPteEntries(bus_mapping.get(), gpu_addr, mapping_page_count);
  }

  void Clear(uint32_t gpu_addr, uint32_t size_in_pages) {
    auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
    auto bus_mapping = owner_.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, size_in_pages);
    EXPECT_TRUE(address_space_->Clear(gpu_addr, bus_mapping.get()));
    CheckPteEntriesClear(gpu_addr, size_in_pages);
  }

  void InsertAndClear(uint32_t gpu_addr, uint32_t size_in_pages, uint32_t mapping_page_count) {
    auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
    auto bus_mapping = owner_.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, mapping_page_count);
    EXPECT_TRUE(address_space_->Insert(gpu_addr, bus_mapping.get()));
    EXPECT_TRUE(address_space_->Clear(gpu_addr, bus_mapping.get()));
    CheckPteEntriesClear(gpu_addr, mapping_page_count);
  }
};

TEST_F(TestAddressSpace, Init) {
  constexpr uint32_t kPageCount = 1000;
  CheckPteEntriesClear(0, kPageCount);
  CheckPteEntriesClear((1 << 31) - kPageCount * PAGE_SIZE, kPageCount);
}

TEST_F(TestAddressSpace, InsertAtStart) { Insert(0, 10, 10); }

TEST_F(TestAddressSpace, InsertAndClearAtStart) { InsertAndClear(0, 10, 10); }

TEST_F(TestAddressSpace, InsertAtEnd) { Insert((1ul << 32) - PAGE_SIZE, 1, 1); }

TEST_F(TestAddressSpace, InsertAndClearAtEnd) { InsertAndClear((1ul << 32) - PAGE_SIZE, 1, 1); }

TEST_F(TestAddressSpace, Clear) { Clear(0, 10); }

TEST_F(TestAddressSpace, InsertShort) { Insert(0, 10, 5); }

TEST_F(TestAddressSpace, InsertShortAndClear) { InsertAndClear(0, 10, 5); }

TEST_F(TestAddressSpace, GarbageCollect) {
  uint32_t gpu_addr = 0x1000000;
  uint32_t page_directory_index =
      (gpu_addr >> (PAGE_SHIFT + AddressSpace::kPageTableShift)) & AddressSpace::kPageDirectoryMask;

  EXPECT_EQ(0u, address_space_->root_->valid_count(page_directory_index));

  uint32_t size_in_pages = 1024 + 1;
  auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
  auto bus_mapping = owner_.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, size_in_pages);

  // Insert 1st
  EXPECT_TRUE(address_space_->Insert(gpu_addr, bus_mapping.get()));
  CheckPteEntries(bus_mapping.get(), gpu_addr, size_in_pages);

  EXPECT_EQ(1024u, address_space_->root_->valid_count(page_directory_index));
  EXPECT_NE(nullptr, address_space_->root_->GetPageTable(page_directory_index, false));

  EXPECT_EQ(1u, address_space_->root_->valid_count(page_directory_index + 1));
  EXPECT_NE(nullptr, address_space_->root_->GetPageTable(page_directory_index + 1, false));

  // Insert 2nd
  EXPECT_TRUE(address_space_->Insert(gpu_addr + PAGE_SIZE * size_in_pages, bus_mapping.get()));
  CheckPteEntries(bus_mapping.get(), gpu_addr + PAGE_SIZE * size_in_pages, size_in_pages);

  EXPECT_EQ(1024u, address_space_->root_->valid_count(page_directory_index + 1));
  EXPECT_NE(nullptr, address_space_->root_->GetPageTable(page_directory_index + 1, false));

  EXPECT_EQ(2u, address_space_->root_->valid_count(page_directory_index + 2));
  EXPECT_NE(nullptr, address_space_->root_->GetPageTable(page_directory_index + 2, false));

  // Clear 1st
  EXPECT_TRUE(address_space_->Clear(gpu_addr, bus_mapping.get()));
  CheckPteEntriesClear(gpu_addr, size_in_pages);

  EXPECT_EQ(0u, address_space_->root_->valid_count(page_directory_index));
  EXPECT_EQ(nullptr, address_space_->root_->GetPageTable(page_directory_index, false));

  EXPECT_EQ(1023u, address_space_->root_->valid_count(page_directory_index + 1));
  EXPECT_NE(nullptr, address_space_->root_->GetPageTable(page_directory_index + 1, false));

  EXPECT_EQ(2u, address_space_->root_->valid_count(page_directory_index + 2));
  EXPECT_NE(nullptr, address_space_->root_->GetPageTable(page_directory_index + 2, false));

  // Clear 2nd
  EXPECT_TRUE(address_space_->Clear(gpu_addr + PAGE_SIZE * size_in_pages, bus_mapping.get()));
  CheckPteEntriesClear(gpu_addr + PAGE_SIZE * size_in_pages, size_in_pages);

  EXPECT_EQ(0u, address_space_->root_->valid_count(page_directory_index + 1));
  EXPECT_EQ(nullptr, address_space_->root_->GetPageTable(page_directory_index + 1, false));

  EXPECT_EQ(0u, address_space_->root_->valid_count(page_directory_index + 2));
  EXPECT_EQ(nullptr, address_space_->root_->GetPageTable(page_directory_index + 2, false));
}

TEST_F(TestAddressSpace, Release) {
  AddressSpace* address_space_ptr = address_space_.get();
  address_space_.reset();
  EXPECT_EQ(owner_.address_space_released(), address_space_ptr);
}

TEST_F(TestAddressSpace, ReleaseMapping) {
  constexpr uint64_t kBufferSizeInPages = 1;
  constexpr uint64_t kGpuAddr = 0x10000;

  std::shared_ptr<MsdVsiBuffer> buffer =
      MsdVsiBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");

  std::shared_ptr<GpuMapping> mapping;
  magma::Status status = AddressSpace::MapBufferGpu(
      address_space_, buffer, kGpuAddr, 0 /*page_offset */, kBufferSizeInPages, &mapping);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(mapping->Release(nullptr /* bus_mappings_out */));
}
