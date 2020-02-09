// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_manager.h"
#include "address_space.h"
#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"
#include "mock/mock_mmio.h"
#include "platform_mmio.h"
#include "registers.h"

class FakeAddressSpaceOwner : public std::enable_shared_from_this<FakeAddressSpaceOwner>,
                              public AddressSpace::Owner {
 public:
  FakeAddressSpaceOwner() : address_manager_(nullptr, 8) {}
  AddressSpaceObserver* GetAddressSpaceObserver() override { return &address_manager_; }
  std::shared_ptr<AddressSpace::Owner> GetSharedPtr() override { return shared_from_this(); }
  magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

 private:
  AddressManager address_manager_;
  MockBusMapper bus_mapper_;
};

class TestAddressSpace {
 public:
  static mali_pte_t get_pte(AddressSpace* address_space, gpu_addr_t gpu_addr) {
    mali_pte_t result;
    EXPECT_TRUE(address_space->ReadPteForTesting(gpu_addr, &result));
    return result;
  }

  static void check_pte_entries_clear(AddressSpace* address_space, uint64_t gpu_addr,
                                      uint64_t size) {
    ASSERT_NE(address_space, nullptr);
    auto page_directory = address_space->root_page_directory_.get();
    constexpr uint32_t kRootDirectoryShift =
        AddressSpace::kPageOffsetBits * (AddressSpace::kPageDirectoryLevels - 1) + PAGE_SHIFT;
    uint64_t offset = (gpu_addr >> kRootDirectoryShift) & AddressSpace::kPageTableMask;

    // This are no other buffers nearby, so levels 2, 1, and 0 should have
    // been cleared and removed.
    EXPECT_EQ(2u, page_directory->gpu()->entry[offset]);
    EXPECT_EQ(nullptr, page_directory->next_levels_[offset]);
  }

  static void check_pte_entries(AddressSpace* address_space,
                                magma::PlatformBusMapper::BusMapping* bus_mapping,
                                uint64_t gpu_addr, uint64_t page_offset, uint64_t flags) {
    ASSERT_NE(address_space, nullptr);

    std::vector<uint64_t>& bus_addr = bus_mapping->Get();

    for (unsigned int i = 0; i < bus_addr.size(); i++) {
      uint64_t pte = get_pte(address_space, gpu_addr + i * PAGE_SIZE);
      static constexpr uint64_t kFlagBits = (1l << 54) | (0xf << 6);
      EXPECT_EQ(pte & ~kFlagBits & ~(PAGE_SIZE - 1), bus_addr[i]);

      EXPECT_EQ(1u, pte & 3);
      EXPECT_EQ(flags, pte & kFlagBits);
    }
  }

  static void Init() {
    FakeAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner, false);

    check_pte_entries_clear(address_space.get(), 0, 1024);
  }

  static void CoherentPageTable() {
    FakeAddressSpaceOwner owner;
    auto coherent_address_space = AddressSpace::Create(&owner, true);
    EXPECT_EQ((1u << 4) | (1u << 2) | (3u),
              0x1f & coherent_address_space->translation_table_entry());

    auto address_space = AddressSpace::Create(&owner, false);
    EXPECT_EQ((1u << 2) | (3u), 0x1f & address_space->translation_table_entry());
  }

  static void Insert() {
    FakeAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner, false);

    // create some buffers
    std::vector<uint64_t> addr = {PAGE_SIZE * 0xbdefcccef, PAGE_SIZE * 100};
    std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);
    std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mapping(2);

    buffer[0] = magma::PlatformBuffer::Create(1000, "test");
    buffer[1] = magma::PlatformBuffer::Create(10000, "test");

    bus_mapping[0] =
        owner.GetBusMapper()->MapPageRangeBus(buffer[0].get(), 0, buffer[0]->size() / PAGE_SIZE);
    bus_mapping[1] =
        owner.GetBusMapper()->MapPageRangeBus(buffer[1].get(), 0, buffer[1]->size() / PAGE_SIZE);

    EXPECT_TRUE(address_space->Insert(addr[0], bus_mapping[0].get(), 0, buffer[0]->size(),
                                      kAccessFlagRead | kAccessFlagNoExecute));

    check_pte_entries(address_space.get(), bus_mapping[0].get(), addr[0], 0, (1 << 6) | (1l << 54));

    EXPECT_TRUE(address_space->Insert(addr[1], bus_mapping[1].get(), 0, buffer[1]->size(),
                                      kAccessFlagWrite | kAccessFlagShareBoth));

    check_pte_entries(address_space.get(), bus_mapping[1].get(), addr[1], 0, (2 << 8) | (1 << 7));

    auto page_directory = address_space->root_page_directory_.get();
    for (int i = 3; i >= 0; i--) {
      uint64_t offset = (addr[0] >> (9 * i + PAGE_SHIFT)) & AddressSpace::kPageTableMask;

      uint64_t entry_flags = i > 0 ? 3u : 1u;
      EXPECT_EQ(entry_flags, page_directory->gpu()->entry[offset] & 3u);
      EXPECT_TRUE(page_directory->gpu()->entry[offset] & ~511);
      if (i > 0)
        page_directory = page_directory->next_levels_[offset].get();
      else
        EXPECT_EQ(0u, page_directory->next_levels_.size());
    }

    EXPECT_TRUE(address_space->Clear(addr[1], buffer[1]->size()));

    check_pte_entries_clear(address_space.get(), addr[1], buffer[1]->size());

    EXPECT_TRUE(address_space->Clear(addr[0], buffer[0]->size()));

    check_pte_entries_clear(address_space.get(), addr[0], buffer[0]->size());

    // Clear entries that don't exist yet.
    EXPECT_TRUE(address_space->Clear(PAGE_SIZE * 1024, PAGE_SIZE * 5));

    EXPECT_TRUE(address_space->Clear((1l << 48) - PAGE_SIZE * 10, PAGE_SIZE * 10));

    // Extend outside of address space.
    EXPECT_FALSE(address_space->Clear((1l << 48) - PAGE_SIZE * 10, PAGE_SIZE * 11));

    EXPECT_FALSE(address_space->Insert((1l << 48) - PAGE_SIZE, bus_mapping[1].get(), 0,
                                       buffer[1]->size(), kAccessFlagRead | kAccessFlagNoExecute));
  }

  static void InsertOffset() {
    FakeAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner, false);

    static constexpr uint64_t kAddr = PAGE_SIZE * 100;

    auto buffer = magma::PlatformBuffer::Create(10000, "test");
    auto bus_mapping = owner.GetBusMapper()->MapPageRangeBus(
        buffer.get(), 1, (buffer->size() - PAGE_SIZE) / PAGE_SIZE);

    EXPECT_TRUE(address_space->Insert(kAddr, bus_mapping.get(), PAGE_SIZE,
                                      buffer->size() - PAGE_SIZE,
                                      kAccessFlagRead | kAccessFlagNoExecute));

    check_pte_entries(address_space.get(), bus_mapping.get(), kAddr, 1, (1 << 6) | (1l << 54));
  }

  static void GarbageCollect() {
    FakeAddressSpaceOwner owner;
    auto address_space = AddressSpace::Create(&owner, false);

    // buffer[0] should overlap two level 0 page tables.
    constexpr uint64_t kInitialAddress = PAGE_SIZE * 511;
    // create some buffers
    std::vector<uint64_t> addr = {kInitialAddress, kInitialAddress + PAGE_SIZE * 5};
    std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);
    std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mapping(2);

    buffer[0] = magma::PlatformBuffer::Create(PAGE_SIZE * 5, "test");
    buffer[1] = magma::PlatformBuffer::Create(PAGE_SIZE * 10, "test");

    bus_mapping[0] =
        owner.GetBusMapper()->MapPageRangeBus(buffer[0].get(), 0, buffer[0]->size() / PAGE_SIZE);
    bus_mapping[1] =
        owner.GetBusMapper()->MapPageRangeBus(buffer[1].get(), 0, buffer[1]->size() / PAGE_SIZE);

    EXPECT_TRUE(address_space->Insert(addr[0], bus_mapping[0].get(), 0, buffer[0]->size(),
                                      kAccessFlagRead | kAccessFlagNoExecute));
    check_pte_entries(address_space.get(), bus_mapping[0].get(), addr[0], 0, (1 << 6) | (1l << 54));

    EXPECT_TRUE(address_space->Insert(addr[1], bus_mapping[1].get(), 0, buffer[1]->size(),
                                      kAccessFlagRead | kAccessFlagNoExecute));

    EXPECT_TRUE(address_space->Clear(addr[0], buffer[0]->size()));

    // Buffer 1 should remain mapped.
    check_pte_entries(address_space.get(), bus_mapping[1].get(), addr[1], 0, (1 << 6) | (1l << 54));

    auto page_directory3 = address_space->root_page_directory_.get();

    EXPECT_EQ(3u, page_directory3->gpu()->entry[0] & 3u);
    EXPECT_TRUE(page_directory3->gpu()->entry[0] & ~511);
    auto page_directory2 = page_directory3->next_levels_[0].get();

    EXPECT_EQ(3u, page_directory2->gpu()->entry[0] & 3u);
    EXPECT_TRUE(page_directory2->gpu()->entry[0] & ~511);
    auto page_directory1 = page_directory2->next_levels_[0].get();

    // The level 0 that's now empty should be removed.
    EXPECT_EQ(2u, page_directory1->gpu()->entry[0] & 3u);
    EXPECT_EQ(0u, page_directory1->gpu()->entry[0] & ~511);
    EXPECT_EQ(nullptr, page_directory1->next_levels_[0].get());

    EXPECT_TRUE(address_space->Clear(addr[1], buffer[1]->size()));

    for (uint32_t i = 0; i < AddressSpace::kPageTableEntries; ++i) {
      EXPECT_EQ(2u, address_space->root_page_directory_->gpu()->entry[i]);
      EXPECT_EQ(nullptr, address_space->root_page_directory_->next_levels_[i]);
    }
  }
};

TEST(AddressSpace, Init) { TestAddressSpace::Init(); }

TEST(AddressSpace, CoherentPageTable) { TestAddressSpace::CoherentPageTable(); }

TEST(AddressSpace, Insert) { TestAddressSpace::Insert(); }

TEST(AddressSpace, InsertOffset) { TestAddressSpace::InsertOffset(); }

TEST(AddressSpace, GarbageCollect) { TestAddressSpace::GarbageCollect(); }
