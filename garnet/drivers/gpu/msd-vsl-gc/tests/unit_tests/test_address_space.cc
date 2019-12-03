// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/address_space.h"
#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"

class TestAddressSpace {
 public:
  class MockAddressSpaceOwner : public AddressSpace::Owner {
   public:
    // Put bus addresses close to the 40 bit limit
    MockAddressSpaceOwner() : bus_mapper_((1ul << (40 - 1))) {}

    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  static void check_pte_entries_clear(AddressSpace* address_space, uint64_t gpu_addr,
                                      uint64_t page_count) {
    for (unsigned int i = 0; i < page_count; i++) {
      uint64_t addr = gpu_addr + i * PAGE_SIZE;
      uint64_t page_table_index = (addr >>= PAGE_SHIFT) & AddressSpace::kPageTableMask;
      uint64_t page_directory_index =
          (addr >>= AddressSpace::kPageTableShift) & AddressSpace::kPageDirectoryMask;

      auto page_table = address_space->root_->GetPageTable(page_directory_index, false);
      auto* pde = address_space->root_->entry(page_directory_index);

      if (page_table) {
        auto* pte = page_table->entry(page_table_index);
        EXPECT_EQ(*pte, AddressSpace::kInvalidPte);
        EXPECT_NE(*pde, AddressSpace::kInvalidPde);
      } else {
        EXPECT_EQ(*pde, AddressSpace::kInvalidPde);
      }
    }
  }

  static void check_pte_entries(AddressSpace* address_space,
                                magma::PlatformBusMapper::BusMapping* bus_mapping,
                                uint64_t gpu_addr, uint64_t mapping_page_count) {
    ASSERT_NE(address_space, nullptr);

    std::vector<uint64_t>& bus_addr = bus_mapping->Get();
    ASSERT_LE(mapping_page_count, bus_addr.size());

    for (unsigned int i = 0; i < mapping_page_count; i++) {
      uint64_t addr = gpu_addr + i * PAGE_SIZE;
      uint64_t page_table_index = (addr >>= PAGE_SHIFT) & AddressSpace::kPageTableMask;
      uint64_t page_directory_index =
          (addr >>= AddressSpace::kPageTableShift) & AddressSpace::kPageDirectoryMask;

      auto page_table = address_space->root_->GetPageTable(page_directory_index, false);
      ASSERT_NE(page_table, nullptr);

      auto* pde = address_space->root_->entry(page_directory_index);
      EXPECT_NE(*pde, AddressSpace::kInvalidPde);

      AddressSpace::pte_t expected_pte;
      EXPECT_TRUE(AddressSpace::pte_encode(bus_addr[i], true, true, true, &expected_pte));
      auto* pte = page_table->entry(page_table_index);
      EXPECT_EQ(*pte, expected_pte);
    }
  }

  static void Init() {
    MockAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner);
    ASSERT_NE(nullptr, address_space);

    constexpr uint32_t kPageCount = 1000;
    check_pte_entries_clear(address_space.get(), 0, kPageCount);
    check_pte_entries_clear(address_space.get(), (1 << 31) - kPageCount * PAGE_SIZE, kPageCount);
  }

  static void Insert(uint64_t gpu_addr, uint32_t size_in_pages, uint32_t mapping_page_count) {
    MockAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner);
    auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
    auto bus_mapping = owner.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, mapping_page_count);
    EXPECT_TRUE(address_space->Insert(gpu_addr, bus_mapping.get()));
    check_pte_entries(address_space.get(), bus_mapping.get(), gpu_addr, mapping_page_count);
  }

  static void Clear(uint64_t gpu_addr, uint32_t size_in_pages) {
    MockAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner);
    auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
    auto bus_mapping = owner.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, size_in_pages);
    EXPECT_TRUE(address_space->Clear(gpu_addr, bus_mapping.get()));
    check_pte_entries_clear(address_space.get(), gpu_addr, size_in_pages);
  }

  static void InsertAndClear(uint64_t gpu_addr, uint32_t size_in_pages,
                             uint32_t mapping_page_count) {
    MockAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner);
    auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
    auto bus_mapping = owner.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, mapping_page_count);
    EXPECT_TRUE(address_space->Insert(gpu_addr, bus_mapping.get()));
    EXPECT_TRUE(address_space->Clear(gpu_addr, bus_mapping.get()));
    check_pte_entries_clear(address_space.get(), gpu_addr, mapping_page_count);
  }

  static void GarbageCollect() {
    MockAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner);

    uint64_t gpu_addr = 0x1000000;
    uint64_t page_directory_index = (gpu_addr >> (PAGE_SHIFT + AddressSpace::kPageTableShift)) &
                                    AddressSpace::kPageDirectoryMask;

    EXPECT_EQ(0u, address_space->root_->valid_count(page_directory_index));

    uint32_t size_in_pages = 1024 + 1;
    auto buffer = magma::PlatformBuffer::Create(size_in_pages * PAGE_SIZE, "test");
    auto bus_mapping = owner.GetBusMapper()->MapPageRangeBus(buffer.get(), 0, size_in_pages);

    // Insert 1st
    EXPECT_TRUE(address_space->Insert(gpu_addr, bus_mapping.get()));
    check_pte_entries(address_space.get(), bus_mapping.get(), gpu_addr, size_in_pages);

    EXPECT_EQ(1024u, address_space->root_->valid_count(page_directory_index));
    EXPECT_NE(nullptr, address_space->root_->GetPageTable(page_directory_index, false));

    EXPECT_EQ(1u, address_space->root_->valid_count(page_directory_index + 1));
    EXPECT_NE(nullptr, address_space->root_->GetPageTable(page_directory_index + 1, false));

    // Insert 2nd
    EXPECT_TRUE(address_space->Insert(gpu_addr + PAGE_SIZE * size_in_pages, bus_mapping.get()));
    check_pte_entries(address_space.get(), bus_mapping.get(), gpu_addr + PAGE_SIZE * size_in_pages,
                      size_in_pages);

    EXPECT_EQ(1024u, address_space->root_->valid_count(page_directory_index + 1));
    EXPECT_NE(nullptr, address_space->root_->GetPageTable(page_directory_index + 1, false));

    EXPECT_EQ(2u, address_space->root_->valid_count(page_directory_index + 2));
    EXPECT_NE(nullptr, address_space->root_->GetPageTable(page_directory_index + 2, false));

    // Clear 1st
    EXPECT_TRUE(address_space->Clear(gpu_addr, bus_mapping.get()));
    check_pte_entries_clear(address_space.get(), gpu_addr, size_in_pages);

    EXPECT_EQ(0u, address_space->root_->valid_count(page_directory_index));
    EXPECT_EQ(nullptr, address_space->root_->GetPageTable(page_directory_index, false));

    EXPECT_EQ(1023u, address_space->root_->valid_count(page_directory_index + 1));
    EXPECT_NE(nullptr, address_space->root_->GetPageTable(page_directory_index + 1, false));

    EXPECT_EQ(2u, address_space->root_->valid_count(page_directory_index + 2));
    EXPECT_NE(nullptr, address_space->root_->GetPageTable(page_directory_index + 2, false));

    // Clear 2nd
    EXPECT_TRUE(address_space->Clear(gpu_addr + PAGE_SIZE * size_in_pages, bus_mapping.get()));
    check_pte_entries_clear(address_space.get(), gpu_addr + PAGE_SIZE * size_in_pages,
                            size_in_pages);

    EXPECT_EQ(0u, address_space->root_->valid_count(page_directory_index + 1));
    EXPECT_EQ(nullptr, address_space->root_->GetPageTable(page_directory_index + 1, false));

    EXPECT_EQ(0u, address_space->root_->valid_count(page_directory_index + 2));
    EXPECT_EQ(nullptr, address_space->root_->GetPageTable(page_directory_index + 2, false));
  }
};

TEST(AddressSpace, Init) { TestAddressSpace::Init(); }

TEST(AddressSpace, InsertAtStart) { TestAddressSpace::Insert(0, 10, 10); }

TEST(AddressSpace, InsertAndClearAtStart) { TestAddressSpace::InsertAndClear(0, 10, 10); }

TEST(AddressSpace, InsertAtEnd) { TestAddressSpace::Insert((1ul << 32) - PAGE_SIZE, 1, 1); }

TEST(AddressSpace, InsertAndClearAtEnd) {
  TestAddressSpace::InsertAndClear((1ul << 32) - PAGE_SIZE, 1, 1);
}

TEST(AddressSpace, Clear) { TestAddressSpace::Clear(0, 10); }

TEST(AddressSpace, InsertShort) { TestAddressSpace::Insert(0, 10, 5); }

TEST(AddressSpace, InsertShortAndClear) { TestAddressSpace::InsertAndClear(0, 10, 5); }

TEST(AddressSpace, GarbageCollect) { TestAddressSpace::GarbageCollect(); }
