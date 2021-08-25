// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include <gtest/gtest.h>

#include "address_manager.h"
#include "fake_connection_owner_base.h"
#include "gpu_mapping.h"
#include "magma_arm_mali_types.h"
#include "mock/mock_bus_mapper.h"
#include "msd_arm_buffer.h"
#include "msd_arm_connection.h"
#include "msd_arm_context.h"
#include "msd_defs.h"

namespace {

template <typename T>
static T ReadValueFromBuffer(MsdArmBuffer* buffer, uint64_t offset) {
  // Map and memcpy instead of PlatformBuffer::Read to ensure this function works with
  // write-combining VMOS.
  void* cpu_addr;
  EXPECT_TRUE(buffer->platform_buffer()->MapCpu(&cpu_addr));
  T value;
  memcpy(&value, reinterpret_cast<uint8_t*>(cpu_addr) + offset, sizeof(T));
  buffer->platform_buffer()->UnmapCpu();
  return value;
}

class TestAddressSpaceObserver : public AddressSpaceObserver {
 public:
  void FlushAddressMappingRange(AddressSpace*, uint64_t start, uint64_t length,
                                bool synchronous) override {}
  void UnlockAddressSpace(AddressSpace* address_space) override {
    unlocked_address_spaces_.push_back(address_space);
  }
  void ReleaseSpaceMappings(const AddressSpace* address_space) override {}

  const std::vector<AddressSpace*>& unlocked_address_spaces() const {
    return unlocked_address_spaces_;
  }

 private:
  std::vector<AddressSpace*> unlocked_address_spaces_;
};

class FakeConnectionOwner : public FakeConnectionOwnerBase {
 public:
  FakeConnectionOwner() {}

  void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override { atoms_list_.push_back(atom); }
  void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) override {
    cancel_atoms_list_.push_back(connection.get());
  }
  AddressSpaceObserver* GetAddressSpaceObserver() override { return &observer_; }
  TestAddressSpaceObserver* GetTestAddressSpaceObserver() { return &observer_; }
  magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }
  ArmMaliCacheCoherencyStatus cache_coherency_status() override {
    return kArmMaliCacheCoherencyAce;
  }
  void SetCurrentThreadToDefaultPriority() override { got_set_to_default_priority_ = true; }
  virtual MagmaMemoryPressureLevel GetCurrentMemoryPressureLevel() override {
    return memory_pressure_level_;
  }

  const std::vector<MsdArmConnection*>& cancel_atoms_list() { return cancel_atoms_list_; }
  const std::vector<std::shared_ptr<MsdArmAtom>>& atoms_list() { return atoms_list_; }
  bool got_set_to_default_priority() const { return got_set_to_default_priority_; }
  void set_memory_pressure_level(MagmaMemoryPressureLevel level) { memory_pressure_level_ = level; }

 private:
  TestAddressSpaceObserver observer_;
  MockConsistentBusMapper bus_mapper_;
  std::vector<MsdArmConnection*> cancel_atoms_list_;
  std::vector<std::shared_ptr<MsdArmAtom>> atoms_list_;
  bool got_set_to_default_priority_ = false;
  MagmaMemoryPressureLevel memory_pressure_level_ = MAGMA_MEMORY_PRESSURE_LEVEL_NORMAL;
};

class DeregisterConnectionOwner : public FakeConnectionOwner {
 public:
  void set_connection(std::weak_ptr<MsdArmConnection> connection) { connection_ = connection; }
  void DeregisterConnection() override { EXPECT_TRUE(connection_.expired()); }

 private:
  std::weak_ptr<MsdArmConnection> connection_;
};

void* g_test_token;
uint32_t g_test_data_size;
magma_arm_mali_status g_status;

void TestCallback(void* token, msd_notification_t* notification) {
  g_test_token = token;
  if (notification->type == MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND) {
    g_test_data_size = notification->u.channel_send.size;
    memcpy(&g_status, notification->u.channel_send.data, g_test_data_size);
  }
}
}  // namespace

class TestConnection {
 public:
  void MapUnmap() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = PAGE_SIZE * 100;

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);

    // GPU VA not page aligned
    EXPECT_FALSE(
        connection->AddMapping(std::make_unique<GpuMapping>(1, 0, 1, 0, connection.get(), buffer)));

    // Empty GPU VA.
    EXPECT_FALSE(connection->AddMapping(
        std::make_unique<GpuMapping>(PAGE_SIZE, 0, 0, 0, connection.get(), buffer)));

    // size would overflow.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        1000 * PAGE_SIZE, 0, std::numeric_limits<uint64_t>::max() - PAGE_SIZE * 100 + 1, 0,
        connection.get(), buffer)));

    // GPU VA would be larger than 48 bits wide.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        1000 * PAGE_SIZE, 0, (1ul << 48) - 999 * PAGE_SIZE, 0, connection.get(), buffer)));

    // Map is too large for buffer.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        1000 * PAGE_SIZE, 0, PAGE_SIZE * 101, 0, connection.get(), buffer)));

    // Map is past end of buffer due to offset.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        1000 * PAGE_SIZE, 1, PAGE_SIZE * 100, 0, connection.get(), buffer)));

    // Page offset would overflow.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        1000 * PAGE_SIZE, std::numeric_limits<uint64_t>::max() / PAGE_SIZE, PAGE_SIZE * 100, 0,
        connection.get(), buffer)));

    // Invalid flags.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        1000 * PAGE_SIZE, 0, PAGE_SIZE * 100, (1 << 14), connection.get(), buffer)));

    EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
        1000 * PAGE_SIZE, 0, PAGE_SIZE * 100, 0, connection.get(), buffer)));

    // Mapping would overlap previous mapping.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        1001 * PAGE_SIZE, 0, PAGE_SIZE * 99, 0, connection.get(), buffer)));

    // Mapping would overlap next mapping.
    EXPECT_FALSE(connection->AddMapping(std::make_unique<GpuMapping>(
        999 * PAGE_SIZE, 0, PAGE_SIZE * 100, 0, connection.get(), buffer)));

    EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
        1100 * PAGE_SIZE, 0, PAGE_SIZE * 100, 0, connection.get(), buffer)));

    EXPECT_FALSE(connection->RemoveMapping(1001 * PAGE_SIZE));

    EXPECT_TRUE(connection->RemoveMapping(1000 * PAGE_SIZE));

    buffer.reset();

    // Mapping should already have been removed by buffer deletion.
    EXPECT_FALSE(connection->RemoveMapping(1100 * PAGE_SIZE));
  }

  static const std::unique_ptr<magma::PlatformBusMapper::BusMapping>& GetBusMapping(
      GpuMapping* gpu_mapping, uint32_t index) {
    return *std::next(gpu_mapping->bus_mappings_.begin(), index);
  }

  void CommitMemory() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = PAGE_SIZE * 100;
    AddressSpace* address_space = connection->address_space_for_testing();

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);

    constexpr uint64_t kGpuOffset[] = {1000, 1100};

    auto mapping0 = std::make_unique<GpuMapping>(kGpuOffset[0] * PAGE_SIZE, 1, PAGE_SIZE * 99, 0,
                                                 connection.get(), buffer);
    GpuMapping* mapping0_ptr = mapping0.get();
    EXPECT_TRUE(connection->AddMapping(std::move(mapping0)));

    EXPECT_TRUE(connection->SetCommittedPagesForBuffer(buffer.get(), 1, 1));
    mali_pte_t pte;
    constexpr uint64_t kInvalidPte = 2u;
    // Only the first page should be committed.
    EXPECT_TRUE(address_space->ReadPteForTesting(kGpuOffset[0] * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 1) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    // Should be legal to map with pages already committed.
    EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
        kGpuOffset[1] * PAGE_SIZE, 1, PAGE_SIZE * 2, 0, connection.get(), buffer)));

    EXPECT_TRUE(address_space->ReadPteForTesting(kGpuOffset[1] * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);

    EXPECT_TRUE(connection->SetCommittedPagesForBuffer(buffer.get(), 1, 5));

    ASSERT_EQ(2u, mapping0_ptr->bus_mappings_.size());
    EXPECT_EQ(1u, GetBusMapping(mapping0_ptr, 0)->page_count());
    EXPECT_EQ(2u, GetBusMapping(mapping0_ptr, 1)->page_offset());
    EXPECT_EQ(4u, GetBusMapping(mapping0_ptr, 1)->page_count());

    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[1] + 1) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    // The mapping should be truncated because it's only for 2 pages.
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[1] + 2) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);

    EXPECT_TRUE(connection->RemoveMapping(kGpuOffset[1] * PAGE_SIZE));

    // Should unmap the last page.
    EXPECT_TRUE(connection->SetCommittedPagesForBuffer(buffer.get(), 1, 4));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);
    ASSERT_EQ(1u, mapping0_ptr->bus_mappings_.size());
    EXPECT_EQ(1u, (*mapping0_ptr->bus_mappings_.begin())->page_offset());
    EXPECT_EQ(4u, (*mapping0_ptr->bus_mappings_.begin())->page_count());
    EXPECT_EQ(4u, mapping0_ptr->committed_region().length());

    // Should be legal even though the region is different from the start. However, it shouldn't
    // mess with pages before the region.
    EXPECT_TRUE(connection->SetCommittedPagesForBuffer(buffer.get(), 0, 6));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] - 1) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    EXPECT_TRUE(connection->SetCommittedPagesForBuffer(buffer.get(), 2, 6));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0]) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 1) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);

    // Can decommit entire buffer.
    EXPECT_TRUE(connection->SetCommittedPagesForBuffer(buffer.get(), 1, 0));
    EXPECT_FALSE(address_space->ReadPteForTesting(kGpuOffset[0] * PAGE_SIZE, &pte));
  }

  void CommitDecommitMemory() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = PAGE_SIZE * 100;
    AddressSpace* address_space = connection->address_space_for_testing();

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);

    constexpr uint64_t kGpuOffset[] = {1000, 1100};

    auto mapping0 = std::make_unique<GpuMapping>(kGpuOffset[0] * PAGE_SIZE, 1, PAGE_SIZE * 99, 0,
                                                 connection.get(), buffer);
    GpuMapping* mapping0_ptr = mapping0.get();
    EXPECT_TRUE(connection->AddMapping(std::move(mapping0)));

    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 1));
    mali_pte_t pte;
    constexpr uint64_t kInvalidPte = 2u;
    // Only the first page should be committed.
    EXPECT_TRUE(address_space->ReadPteForTesting(kGpuOffset[0] * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 1) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    // Should be legal to map with pages already committed.
    EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
        kGpuOffset[1] * PAGE_SIZE, 1, PAGE_SIZE * 2, 0, connection.get(), buffer)));

    EXPECT_TRUE(address_space->ReadPteForTesting(kGpuOffset[1] * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);

    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 5));

    ASSERT_EQ(2u, mapping0_ptr->bus_mappings_.size());
    EXPECT_EQ(1u, GetBusMapping(mapping0_ptr, 0)->page_count());
    EXPECT_EQ(2u, GetBusMapping(mapping0_ptr, 1)->page_offset());
    EXPECT_EQ(4u, GetBusMapping(mapping0_ptr, 1)->page_count());

    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[1] + 1) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    // The mapping should be truncated because it's only for 2 pages.
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[1] + 2) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);

    EXPECT_TRUE(connection->RemoveMapping(kGpuOffset[1] * PAGE_SIZE));

    // Shouldn't actually do anything.
    EXPECT_TRUE(connection->DecommitMemoryForBuffer(buffer.get(), 6, 0));

    // Should unmap the last page.
    EXPECT_TRUE(connection->DecommitMemoryForBuffer(buffer.get(), 5, 5));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);
    ASSERT_EQ(1u, mapping0_ptr->bus_mappings_.size());
    EXPECT_EQ(1u, GetBusMapping(mapping0_ptr, 0)->page_offset());
    EXPECT_EQ(4u, GetBusMapping(mapping0_ptr, 0)->page_count());
    EXPECT_EQ(4u, mapping0_ptr->committed_region().length());

    // Change the offset lower.
    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 0, 6));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    // Shouldn't try to modify pages before the start of the mapping.
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] - 1) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    // Committing smaller range shouldn't do anything.
    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 0));
    EXPECT_TRUE(address_space->ReadPteForTesting(kGpuOffset[0] * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);

    // Decommit the lowest two pages.
    EXPECT_TRUE(connection->DecommitMemoryForBuffer(buffer.get(), 0, 2));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0]) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] - 1) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    // Decommit entire buffer.
    EXPECT_TRUE(connection->DecommitMemoryForBuffer(buffer.get(), 0, 6));
    EXPECT_FALSE(address_space->ReadPteForTesting(kGpuOffset[0] * PAGE_SIZE, &pte));
  }

  void CommitLargeBuffer() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = 1ul << 35;  // 32 GB

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);
    MsdArmAbiBuffer abi_buffer(buffer);

    constexpr uint64_t kGpuOffset[] = {1000, 1100};

    EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
        kGpuOffset[0] * PAGE_SIZE, 0, PAGE_SIZE * 100, 0, connection.get(), buffer)));

    // Committing 1 page should be fine.
    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 0, 1));

    // MockBusMapper will fail committing the entire region.
    EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
        kGpuOffset[1] * PAGE_SIZE, 0, kBufferSize, 0, connection.get(), buffer)));

    EXPECT_FALSE(connection->CommitMemoryForBuffer(buffer.get(), 0, kBufferSize / PAGE_SIZE));
  }

  void GrowableMemory() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = PAGE_SIZE * 100;
    AddressSpace* address_space = connection->address_space_for_testing();

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);

    constexpr uint64_t kGpuOffset[] = {1000, 1100};

    EXPECT_TRUE(connection->AddMapping(
        std::make_unique<GpuMapping>(kGpuOffset[0] * PAGE_SIZE, 1, PAGE_SIZE * 95,
                                     MAGMA_GPU_MAP_FLAG_GROWABLE, connection.get(), buffer)));
    EXPECT_TRUE(connection->AddMapping(
        std::make_unique<GpuMapping>(kGpuOffset[1] * PAGE_SIZE, 1, PAGE_SIZE * 95,
                                     MAGMA_GPU_MAP_FLAG_GROWABLE, connection.get(), buffer)));

    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 1));
    mali_pte_t pte;
    constexpr uint64_t kInvalidPte = 2u;
    // Only the first page should be committed.
    EXPECT_TRUE(address_space->ReadPteForTesting(kGpuOffset[0] * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 1) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    EXPECT_FALSE(connection->PageInMemory((kGpuOffset[0] + 95) * PAGE_SIZE));

    // Should grow to a 64-page boundary.
    EXPECT_TRUE(connection->PageInMemory((kGpuOffset[0] + 1) * PAGE_SIZE));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 1) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 63) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 64) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    // Second mapping should also be grown.
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[1] + 1) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);

    // Should be growable up to last page of mapping.
    EXPECT_TRUE(connection->PageInMemory((kGpuOffset[0] + 94) * PAGE_SIZE));
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 94) * PAGE_SIZE, &pte));
    EXPECT_NE(kInvalidPte, pte);
    EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 95) * PAGE_SIZE, &pte));
    EXPECT_EQ(kInvalidPte, pte);

    // Address space size didn't change, so it should be unlocked.
    EXPECT_EQ(0u, owner.GetTestAddressSpaceObserver()->unlocked_address_spaces().size());
    EXPECT_TRUE(connection->PageInMemory((kGpuOffset[0] + 94) * PAGE_SIZE));
    EXPECT_LE(1u, owner.GetTestAddressSpaceObserver()->unlocked_address_spaces().size());
  }

  void Notification() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    MsdArmAtom atom(connection, 0, 1, 5, magma_arm_mali_user_data{7, 8}, 0);

    atom.set_result_code(static_cast<ArmMaliResultCode>(10));
    // Shouldn't do anything.
    connection->SendNotificationData(&atom);

    uint32_t token;
    connection->SetNotificationCallback(&TestCallback, &token);
    MsdArmAtom atom2(connection, 0, 1, 5, magma_arm_mali_user_data{7, 8}, 0);

    atom2.set_result_code(static_cast<ArmMaliResultCode>(20));
    connection->SendNotificationData(&atom2);
    EXPECT_EQ(sizeof(g_status), g_test_data_size);
    EXPECT_EQ(&token, g_test_token);

    EXPECT_EQ(7u, g_status.data.data[0]);
    EXPECT_EQ(8u, g_status.data.data[1]);
    EXPECT_EQ(20u, g_status.result_code);
    EXPECT_EQ(5u, g_status.atom_number);

    connection->SetNotificationCallback(nullptr, nullptr);
    connection->SendNotificationData(&atom);

    EXPECT_EQ(20u, g_status.result_code);
  }

  void DestructionNotification() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);

    uint32_t token;
    connection->SetNotificationCallback(&TestCallback, &token);
    connection->MarkDestroyed();

    EXPECT_TRUE(owner.got_set_to_default_priority());

    EXPECT_EQ(sizeof(g_status), g_test_data_size);
    EXPECT_EQ(&token, g_test_token);

    EXPECT_EQ(0u, g_status.data.data[0]);
    EXPECT_EQ(0u, g_status.data.data[1]);
    EXPECT_EQ(0u, g_status.atom_number);
    EXPECT_EQ(kArmMaliResultTerminated, g_status.result_code);

    // Shouldn't do anything.
    MsdArmAtom atom(connection, 0, 1, 5, magma_arm_mali_user_data{7, 8}, 0);
    atom.set_result_code(static_cast<ArmMaliResultCode>(10));
    connection->SendNotificationData(&atom);
    EXPECT_EQ(kArmMaliResultTerminated, g_status.result_code);

    connection->SetNotificationCallback(nullptr, 0);

    EXPECT_EQ(1u, owner.cancel_atoms_list().size());
    EXPECT_EQ(connection.get(), owner.cancel_atoms_list()[0]);
  }

  void SoftwareAtom() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);

    magma_arm_mali_atom client_atom = {};
    client_atom.flags = kAtomFlagSemaphoreWait;
    std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;
    size_t remaining_size = sizeof(magma_arm_mali_atom);
    EXPECT_FALSE(connection->ExecuteAtom(&remaining_size, &client_atom, &semaphores));

    std::shared_ptr<magma::PlatformSemaphore> semaphore(magma::PlatformSemaphore::Create());
    semaphores.push_back(semaphore);
    remaining_size = sizeof(magma_arm_mali_atom);
    EXPECT_TRUE(connection->ExecuteAtom(&remaining_size, &client_atom, &semaphores));

    EXPECT_EQ(1u, owner.atoms_list().size());
    std::shared_ptr<MsdArmAtom> atom = owner.atoms_list()[0];
    std::shared_ptr<MsdArmSoftAtom> soft_atom = MsdArmSoftAtom::cast(atom);
    EXPECT_TRUE(!!soft_atom);
    EXPECT_EQ(kAtomFlagSemaphoreWait, soft_atom->soft_flags());
    EXPECT_EQ(semaphore, soft_atom->platform_semaphore());
  }

  void FlushRegion() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = PAGE_SIZE * 100;

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);

    constexpr uint64_t kGpuOffset[] = {1000, 1100, 1200};

    auto mapping0 = std::make_unique<GpuMapping>(kGpuOffset[0] * PAGE_SIZE, 1, PAGE_SIZE * 5, 0,
                                                 connection.get(), buffer);
    EXPECT_TRUE(connection->AddMapping(std::move(mapping0)));

    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 99));
    EXPECT_EQ(static_cast<uint32_t>(PAGE_SIZE), buffer->flushed_region_.start());
    EXPECT_EQ(static_cast<uint32_t>(PAGE_SIZE * 6), buffer->flushed_region_.end());

    auto mapping1 = std::make_unique<GpuMapping>(kGpuOffset[1] * PAGE_SIZE, 1, PAGE_SIZE * 6, 0,
                                                 connection.get(), buffer);
    EXPECT_TRUE(connection->AddMapping(std::move(mapping1)));

    EXPECT_EQ(static_cast<uint32_t>(PAGE_SIZE), buffer->flushed_region_.start());
    EXPECT_EQ(static_cast<uint32_t>(PAGE_SIZE * 7), buffer->flushed_region_.end());

    // Outer cache-coherent mappings shouldn't flush pages.
    auto mapping2 = std::make_unique<GpuMapping>(kGpuOffset[2] * PAGE_SIZE, 1, PAGE_SIZE * 99,
                                                 kMagmaArmMaliGpuMapFlagBothShareable,
                                                 connection.get(), buffer);
    EXPECT_TRUE(connection->AddMapping(std::move(mapping2)));
    EXPECT_EQ(static_cast<uint32_t>(PAGE_SIZE), buffer->flushed_region_.start());
    EXPECT_EQ(static_cast<uint32_t>(PAGE_SIZE * 7), buffer->flushed_region_.end());
  }

  void FlushUncachedRegion() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = PAGE_SIZE * 100;

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);
    EXPECT_TRUE(buffer->platform_buffer()->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED));

    constexpr uint64_t kGpuOffset = 1000;
    auto mapping = std::make_unique<GpuMapping>(kGpuOffset * PAGE_SIZE, 1, PAGE_SIZE * 99, 0,
                                                connection.get(), buffer);
    EXPECT_TRUE(connection->AddMapping(std::move(mapping)));

    // Mappings of uncached buffers shouldn't flush pages.
    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 1));
    EXPECT_EQ(0u, buffer->flushed_region_.start());
    EXPECT_EQ(0u, buffer->flushed_region_.end());
  }

  void PhysicalToVirtual() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    constexpr uint64_t kBufferSize = PAGE_SIZE * 100;

    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);

    constexpr uint64_t kGpuOffset = 1100;
    constexpr uint32_t kMappingOffsetInPages = 1;

    auto mapping = std::make_unique<GpuMapping>(kGpuOffset * PAGE_SIZE, kMappingOffsetInPages,
                                                PAGE_SIZE * 5, 0, connection.get(), buffer);
    EXPECT_TRUE(connection->AddMapping(std::move(mapping)));
    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), kMappingOffsetInPages, 2));

    auto bus_mapping = owner.GetBusMapper()->MapPageRangeBus(buffer->platform_buffer(), 0, 100);
    constexpr uint32_t kPageOffsetIntoBuffer = 2;
    uint64_t physical = bus_mapping->Get()[kPageOffsetIntoBuffer] + 300;
    uint64_t virtual_address;
    EXPECT_TRUE(connection->GetVirtualAddressFromPhysical(physical, &virtual_address));
    EXPECT_EQ((kGpuOffset + kPageOffsetIntoBuffer - kMappingOffsetInPages) * PAGE_SIZE + 300,
              virtual_address);

    // Don't check uncommitted pages inside mapping.
    physical = bus_mapping->Get()[4] + 300;
    EXPECT_FALSE(connection->GetVirtualAddressFromPhysical(physical, &virtual_address));

    // Don't check pages after mapping.
    physical = bus_mapping->Get()[6] + 300;
    EXPECT_FALSE(connection->GetVirtualAddressFromPhysical(physical, &virtual_address));

    // Don't check pages before mapping.
    physical = bus_mapping->Get()[0] + 300;
    EXPECT_FALSE(connection->GetVirtualAddressFromPhysical(physical, &virtual_address));
  }

  void DeregisterConnection() {
    DeregisterConnectionOwner owner;
    {
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);
      owner.set_connection(connection);
    }
  }

  void ContextCount() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);

    EXPECT_EQ(0u, connection->context_count());
    auto context = std::make_unique<MsdArmContext>(connection);
    EXPECT_EQ(1u, connection->context_count());

    auto context2 = std::make_unique<MsdArmContext>(connection);
    EXPECT_EQ(2u, connection->context_count());
    context.reset();
    EXPECT_EQ(1u, connection->context_count());
    connection.reset();
  }

  void JitAddressSpaceAllocate() {
    struct TestingAtom {
      magma_arm_mali_atom atom;
      magma_arm_jit_address_space_allocate_info alloc_info;
    } __attribute__((packed));
    const uint64_t kJitBase = magma::page_size();
    TestingAtom good_atom{};
    good_atom.alloc_info.version_number = 0;
    good_atom.alloc_info.trim_level = 5;
    good_atom.alloc_info.max_allocations = 6;
    good_atom.alloc_info.address = kJitBase;
    good_atom.alloc_info.va_page_count = 1;
    good_atom.atom.atom_number = 1;
    good_atom.atom.size = sizeof(good_atom.atom);
    good_atom.atom.flags = kAtomFlagJitAddressSpaceAllocate;
    std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

    FakeConnectionOwner owner;
    {
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      size_t size = sizeof(good_atom);
      EXPECT_TRUE(connection->ExecuteAtom(&size, &good_atom.atom, &semaphores));
      EXPECT_EQ(0u, size);
      EXPECT_EQ(0u, owner.atoms_list().size());
      {
        std::lock_guard lock(connection->address_lock_);
        EXPECT_EQ(connection->jit_properties_.max_allocations, 6u);
        EXPECT_EQ(connection->jit_properties_.trim_level, 5u);
        EXPECT_EQ(connection->jit_allocator_->base(), kJitBase);
        EXPECT_EQ(connection->jit_allocator_->size(),
                  good_atom.alloc_info.va_page_count * magma::page_size());
      }
      size = sizeof(good_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &good_atom.atom, &semaphores));
    }
    // Invalid version
    {
      TestingAtom bad_atom = good_atom;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      bad_atom.alloc_info.version_number = 1000;
      size_t size = sizeof(good_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }
    // Invalid trim level
    {
      TestingAtom bad_atom = good_atom;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      bad_atom.alloc_info.trim_level = 101;
      size_t size = sizeof(good_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }

    // Invalid size
    {
      TestingAtom bad_atom = good_atom;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      size_t size = sizeof(good_atom) - 1;
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }

    // Invalid va_pages
    {
      TestingAtom bad_atom = good_atom;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      bad_atom.alloc_info.va_page_count = (1ul << 48);
      size_t size = sizeof(good_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }
  }

  void JitParseAllocate() {
    struct TestingAtom {
      magma_arm_mali_atom atom;
      magma_arm_jit_atom_trailer trailer;
      magma_arm_jit_memory_allocate_info info[2];
    } __attribute__((packed));
    TestingAtom good_atom{};
    good_atom.trailer.jit_memory_info_count = 2;
    for (uint32_t i = 0; i < std::size(good_atom.info); i++) {
      auto& info = good_atom.info[i];
      info.id = i;
      info.extend_page_count = 1;
      info.committed_page_count = 1;
      info.address = magma::page_size();
      info.version_number = 0;
    }
    good_atom.atom.atom_number = 1;
    good_atom.atom.size = sizeof(good_atom.atom);
    good_atom.atom.flags = kAtomFlagJitMemoryAllocate;
    std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      size_t size = sizeof(good_atom);
      EXPECT_TRUE(connection->ExecuteAtom(&size, &good_atom.atom, &semaphores));
      EXPECT_EQ(0u, size);
      EXPECT_EQ(1u, owner.atoms_list().size());
      auto atom = owner.atoms_list()[0];
      auto soft_atom = MsdArmSoftAtom::cast(atom);
      EXPECT_TRUE(soft_atom);
      EXPECT_EQ(std::size(good_atom.info), soft_atom->jit_allocate_info().size());
      for (uint32_t i = 0; i < std::size(good_atom.info); i++) {
        EXPECT_EQ(0, memcmp(&good_atom.info[i], &soft_atom->jit_allocate_info()[i],
                            sizeof(good_atom.info[i])));
      }
    }

    // Bad size
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      size_t size = sizeof(good_atom) - 1;
      EXPECT_FALSE(connection->ExecuteAtom(&size, &good_atom.atom, &semaphores));
    }

    // Too many trailing infos.
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      TestingAtom bad_atom = good_atom;
      bad_atom.trailer.jit_memory_info_count = 3;

      size_t size = sizeof(bad_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }

    // Bad version
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      TestingAtom bad_atom = good_atom;
      bad_atom.info[1].version_number = 100;

      size_t size = sizeof(bad_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }

    // Too few trailing infos.
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      TestingAtom bad_atom = good_atom;
      bad_atom.trailer.jit_memory_info_count = 0;

      size_t size = sizeof(bad_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }
  }

  void JitParseFree() {
    struct TestingAtom {
      magma_arm_mali_atom atom;
      magma_arm_jit_atom_trailer trailer;
      magma_arm_jit_memory_free_info info[2];
    } __attribute__((packed));
    TestingAtom good_atom{};
    good_atom.trailer.jit_memory_info_count = 2;
    for (uint32_t i = 0; i < std::size(good_atom.info); i++) {
      auto& info = good_atom.info[i];
      info.id = i;
      info.version_number = 0;
    }
    good_atom.atom.atom_number = 1;
    good_atom.atom.size = sizeof(good_atom.atom);
    good_atom.atom.flags = kAtomFlagJitMemoryFree;
    std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      size_t size = sizeof(good_atom);
      EXPECT_TRUE(connection->ExecuteAtom(&size, &good_atom.atom, &semaphores));
      EXPECT_EQ(0u, size);
      EXPECT_EQ(1u, owner.atoms_list().size());
      auto atom = owner.atoms_list()[0];
      auto soft_atom = MsdArmSoftAtom::cast(atom);
      EXPECT_TRUE(soft_atom);
      EXPECT_EQ(std::size(good_atom.info), soft_atom->jit_free_info().size());
      for (uint32_t i = 0; i < std::size(good_atom.info); i++) {
        EXPECT_EQ(0, memcmp(&good_atom.info[i], &soft_atom->jit_free_info()[i],
                            sizeof(good_atom.info[i])));
      }
    }

    // Bad size
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      size_t size = sizeof(good_atom) - 1;
      EXPECT_FALSE(connection->ExecuteAtom(&size, &good_atom.atom, &semaphores));
    }

    // Too many trailing infos.
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      TestingAtom bad_atom = good_atom;
      bad_atom.trailer.jit_memory_info_count = 3;

      size_t size = sizeof(bad_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }

    // Bad version
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      TestingAtom bad_atom = good_atom;
      bad_atom.info[1].version_number = 100;

      size_t size = sizeof(bad_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }

    // Too few trailing infos.
    {
      FakeConnectionOwner owner;
      auto connection = MsdArmConnection::Create(0, &owner);
      EXPECT_TRUE(connection);

      TestingAtom bad_atom = good_atom;
      bad_atom.trailer.jit_memory_info_count = 0;

      size_t size = sizeof(bad_atom);
      EXPECT_FALSE(connection->ExecuteAtom(&size, &bad_atom.atom, &semaphores));
    }
  }

  void InitializeJitAddressSpace(std::shared_ptr<MsdArmConnection> connection,
                                 uint64_t* start_region_out) {
    struct TestingAddressAllocateAtom {
      magma_arm_mali_atom atom;
      magma_arm_jit_address_space_allocate_info alloc_info;
    } __attribute__((packed));
    const uint64_t kJitBase = magma::page_size();
    TestingAddressAllocateAtom address_space_atom{};
    address_space_atom.alloc_info.version_number = 0;
    address_space_atom.alloc_info.trim_level = 5;
    address_space_atom.alloc_info.max_allocations = 6;
    address_space_atom.alloc_info.address = kJitBase;
    address_space_atom.alloc_info.va_page_count = 10;
    address_space_atom.atom.atom_number = 1;
    address_space_atom.atom.size = sizeof(address_space_atom.atom);
    address_space_atom.atom.flags = kAtomFlagJitAddressSpaceAllocate;
    std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

    size_t size = sizeof(address_space_atom);
    EXPECT_TRUE(connection->ExecuteAtom(&size, &address_space_atom.atom, &semaphores));

    *start_region_out = address_space_atom.alloc_info.address;
  }

  std::shared_ptr<MsdArmBuffer> CreateBufferAtAddress(std::shared_ptr<MsdArmConnection> connection,
                                                      uint64_t address, uint64_t size) {
    std::shared_ptr<MsdArmBuffer> buffer(MsdArmBuffer::Create(size, "test-buffer").release());
    EXPECT_TRUE(buffer);

    auto mapping = std::make_unique<GpuMapping>(address, 0, size, 0, connection.get(), buffer);
    EXPECT_TRUE(connection->AddMapping(std::move(mapping)));
    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 0, size / magma::page_size()));
    return buffer;
  }

  void JitAllocateNormal() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    uint64_t jit_region_start;
    InitializeJitAddressSpace(connection, &jit_region_start);

    constexpr uint32_t kBufferSize = ZX_PAGE_SIZE;
    constexpr uint32_t kAddressPageAddress = ZX_PAGE_SIZE * 100;
    auto buffer = CreateBufferAtAddress(connection, kAddressPageAddress, kBufferSize);

    // Allocate two atoms that together take up the space.
    {
      std::vector<magma_arm_jit_memory_allocate_info> infos(2);
      for (uint32_t i = 0; i < std::size(infos); i++) {
        auto& info = infos[i];
        // ID 0 isn't valid, so use 1 and 2.
        info.id = i + 1;
        info.extend_page_count = 1;
        info.committed_page_count = 1;
        info.address = kAddressPageAddress + (i * 8);
        info.va_page_count = 5;
        info.version_number = 0;
      }
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
          magma_arm_mali_user_data{}, std::move(infos));
      EXPECT_EQ(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));
      // Assume that the first region is allocated before the second region.
      EXPECT_EQ(jit_region_start, ReadValueFromBuffer<uint64_t>(buffer.get(), 0));
      EXPECT_EQ(jit_region_start + 5ul * magma::page_size(),
                ReadValueFromBuffer<uint64_t>(buffer.get(), 8));
    }
    {
      // Try to allocate another region, while there's no enough room in the VA area.
      std::vector<magma_arm_jit_memory_allocate_info> infos(1);
      auto& info = infos[0];
      info.id = 3;
      info.extend_page_count = 1;
      info.committed_page_count = 1;
      info.address = kAddressPageAddress + 16;
      info.va_page_count = 5;
      info.version_number = 0;
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
          magma_arm_mali_user_data{}, std::move(infos));
      EXPECT_FALSE(connection->AllocateJitMemory(msd_atom));
      EXPECT_EQ(kArmMaliResultRunning, msd_atom->result_code());

      std::vector<magma_arm_jit_memory_free_info> free_infos(1);
      free_infos[0].id = 2;

      auto msd_free_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
          magma_arm_mali_user_data{}, std::move(free_infos));
      connection->ReleaseJitMemory(msd_free_atom);

      // The space from the second info is now free and should be reused.
      EXPECT_EQ(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));
      EXPECT_EQ(jit_region_start + 5ul * magma::page_size(),
                ReadValueFromBuffer<uint64_t>(buffer.get(), 16));
    }
  }

  void JitAllocateWriteCombining() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    uint64_t jit_region_start;
    InitializeJitAddressSpace(connection, &jit_region_start);

    constexpr uint32_t kBufferSize = ZX_PAGE_SIZE;
    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
    EXPECT_TRUE(buffer);
    buffer->platform_buffer()->SetCachePolicy(MAGMA_CACHE_POLICY_WRITE_COMBINING);

    constexpr uint32_t kAddressPageAddress = ZX_PAGE_SIZE * 100;
    auto mapping = std::make_unique<GpuMapping>(kAddressPageAddress, 0, kBufferSize, 0,
                                                connection.get(), buffer);
    EXPECT_TRUE(connection->AddMapping(std::move(mapping)));
    EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 0, 1));

    std::vector<magma_arm_jit_memory_allocate_info> infos(1);
    auto& info = infos[0];
    info.id = 1;
    info.extend_page_count = 1;
    info.committed_page_count = 1;
    info.address = kAddressPageAddress;
    // Same as the JIT address space size to ensure the start address is consistent.
    info.va_page_count = 10;
    info.version_number = 0;
    auto msd_atom = std::make_shared<MsdArmSoftAtom>(
        connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
        magma_arm_mali_user_data{}, std::move(infos));
    EXPECT_EQ(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));
    EXPECT_EQ(jit_region_start, ReadValueFromBuffer<uint64_t>(buffer.get(), 0));
  }

  static void ReleaseFreeJitRegions(std::shared_ptr<MsdArmConnection> connection,
                                    std::vector<uint64_t> ids) {
    std::vector<magma_arm_jit_memory_free_info> free_infos(ids.size());
    for (size_t i = 0; i < ids.size(); i++) {
      free_infos[i].id = ids[i];
    }
    auto msd_free_atom = std::make_shared<MsdArmSoftAtom>(
        connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
        magma_arm_mali_user_data{}, std::move(free_infos));
    connection->ReleaseJitMemory(msd_free_atom);
  }

  void JitAllocateReuseChoice() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    uint64_t jit_region_start;
    InitializeJitAddressSpace(connection, &jit_region_start);

    constexpr uint32_t kBufferSize = ZX_PAGE_SIZE;
    constexpr uint32_t kAddressPageAddress = ZX_PAGE_SIZE * 100;
    auto buffer = CreateBufferAtAddress(connection, kAddressPageAddress, kBufferSize);

    // Allocate two atoms that together take up the space.
    {
      std::vector<magma_arm_jit_memory_allocate_info> infos(2);
      for (uint32_t i = 0; i < std::size(infos); i++) {
        auto& info = infos[i];
        // ID 0 isn't valid, so use 1 and 2.
        info.id = i + 1;
        info.usage_id = i + 1;
        info.extend_page_count = 1;
        info.address = kAddressPageAddress + (i * 8);
        info.va_page_count = 5;
        info.version_number = 0;
      }
      infos[0].committed_page_count = 4;
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
          magma_arm_mali_user_data{}, std::move(infos));
      EXPECT_EQ(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));
      // Assume that the first region is allocated before the second region.
      EXPECT_EQ(jit_region_start, ReadValueFromBuffer<uint64_t>(buffer.get(), 0));
      EXPECT_EQ(jit_region_start + 5ul * magma::page_size(),
                ReadValueFromBuffer<uint64_t>(buffer.get(), 8));
    }

    ReleaseFreeJitRegions(connection, {1, 2});

    // Check that the usage is being properly compared.
    {
      std::vector<magma_arm_jit_memory_allocate_info> infos(1);
      auto& info = infos[0];
      info.id = 3;
      info.usage_id = 2;
      info.extend_page_count = 1;
      info.address = kAddressPageAddress + 16;
      info.va_page_count = 5;
      info.version_number = 0;
      info.committed_page_count = 4;
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
          magma_arm_mali_user_data{}, std::move(infos));
      EXPECT_EQ(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));

      // Use the second region because the usage matches, even though the committed page count is
      // the same.
      EXPECT_EQ(jit_region_start + 5ul * magma::page_size(),
                ReadValueFromBuffer<uint64_t>(buffer.get(), 16));
    }

    ReleaseFreeJitRegions(connection, {3});

    // Check with no usages.
    {
      std::vector<magma_arm_jit_memory_allocate_info> infos(1);
      auto& info = infos[0];
      info.id = 3;
      info.usage_id = 0;
      info.extend_page_count = 1;
      info.address = kAddressPageAddress + 24;
      info.va_page_count = 5;
      info.version_number = 0;
      info.committed_page_count = 3;
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
          magma_arm_mali_user_data{}, std::move(infos));
      EXPECT_EQ(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));

      // usage id is 0, so the first jit region should be used because the committed_page_count is
      // the closest.
      EXPECT_EQ(jit_region_start, ReadValueFromBuffer<uint64_t>(buffer.get(), 24));
    }
  }

  void JitAllocateInvalidCommitSize() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    uint64_t jit_region_start;
    InitializeJitAddressSpace(connection, &jit_region_start);

    constexpr uint32_t kBufferSize = ZX_PAGE_SIZE;
    constexpr uint32_t kAddressPageAddress = ZX_PAGE_SIZE * 100;
    auto buffer = CreateBufferAtAddress(connection, kAddressPageAddress, kBufferSize);

    std::vector<magma_arm_jit_memory_allocate_info> infos(1);
    auto& info = infos[0];
    info.id = 1;
    info.usage_id = 1;
    info.extend_page_count = 1;
    info.address = kAddressPageAddress;
    info.va_page_count = 5;
    info.version_number = 0;
    info.committed_page_count = 10;
    auto msd_atom = std::make_shared<MsdArmSoftAtom>(
        connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
        magma_arm_mali_user_data{}, std::move(infos));
    // comitted_pages > va_pages, so the allocation should fail.
    EXPECT_NE(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));
  }

  void JitAllocateInvalidWriteAddress() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    uint64_t jit_region_start;
    InitializeJitAddressSpace(connection, &jit_region_start);

    constexpr uint32_t kBufferSize = ZX_PAGE_SIZE;
    constexpr uint32_t kAddressPageAddress = ZX_PAGE_SIZE * 100;
    auto buffer = CreateBufferAtAddress(connection, kAddressPageAddress, kBufferSize);

    std::vector<magma_arm_jit_memory_allocate_info> infos(1);
    auto& info = infos[0];
    info.id = 1;
    info.usage_id = 1;
    info.extend_page_count = 1;
    info.address = kAddressPageAddress + kBufferSize;
    info.va_page_count = 5;
    info.version_number = 0;
    info.committed_page_count = 1;
    auto msd_atom = std::make_shared<MsdArmSoftAtom>(
        connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
        magma_arm_mali_user_data{}, std::move(infos));
    EXPECT_NE(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));
  }

  void MemoryPressure() {
    FakeConnectionOwner owner;
    auto connection = MsdArmConnection::Create(0, &owner);
    EXPECT_TRUE(connection);
    uint64_t jit_region_start;
    InitializeJitAddressSpace(connection, &jit_region_start);

    constexpr uint32_t kBufferSize = ZX_PAGE_SIZE;
    constexpr uint32_t kAddressPageAddress = ZX_PAGE_SIZE * 100;
    auto buffer = CreateBufferAtAddress(connection, kAddressPageAddress, kBufferSize);

    // Allocate two atoms that together take up the space.
    {
      std::vector<magma_arm_jit_memory_allocate_info> infos(2);
      for (uint32_t i = 0; i < std::size(infos); i++) {
        auto& info = infos[i];
        // ID 0 isn't valid, so use 1 and 2.
        info.id = i + 1;
        info.extend_page_count = 1;
        info.committed_page_count = 1;
        info.address = kAddressPageAddress + (i * 8);
        info.va_page_count = 5;
        info.version_number = 0;
      }
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryAllocate), 1,
          magma_arm_mali_user_data{}, std::move(infos));
      EXPECT_EQ(kArmMaliResultSuccess, *connection->AllocateJitMemory(msd_atom));
      // Assume that the first region is allocated before the second region.
      EXPECT_EQ(jit_region_start, ReadValueFromBuffer<uint64_t>(buffer.get(), 0));
      EXPECT_EQ(jit_region_start + 5ul * magma::page_size(),
                ReadValueFromBuffer<uint64_t>(buffer.get(), 8));
    }

    {
      std::vector<magma_arm_jit_memory_free_info> infos(1);
      infos[0].id = 1;
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryFree), 1, magma_arm_mali_user_data{},
          std::move(infos));
      connection->ReleaseJitMemory(std::move(msd_atom));
    }
    EXPECT_EQ(0u, connection->PeriodicMemoryPressureCallback());
    {
      std::lock_guard lock(connection->address_lock_);
      EXPECT_EQ(2u, connection->jit_memory_regions_.size());
    }

    owner.set_memory_pressure_level(MAGMA_MEMORY_PRESSURE_LEVEL_CRITICAL);

    // ID 1 has 1 committed page.
    EXPECT_EQ(ZX_PAGE_SIZE, connection->PeriodicMemoryPressureCallback());
    {
      std::lock_guard lock(connection->address_lock_);
      EXPECT_EQ(1u, connection->jit_memory_regions_.size());
    }
    {
      std::vector<magma_arm_jit_memory_free_info> infos{{.id = 2}};
      auto msd_atom = std::make_shared<MsdArmSoftAtom>(
          connection, static_cast<AtomFlags>(kAtomFlagJitMemoryFree), 1, magma_arm_mali_user_data{},
          std::move(infos));
      connection->ReleaseJitMemory(std::move(msd_atom));
      std::lock_guard lock(connection->address_lock_);
      EXPECT_EQ(1u, connection->jit_memory_regions_.size());
    }
    EXPECT_EQ(ZX_PAGE_SIZE, connection->PeriodicMemoryPressureCallback());
    {
      std::lock_guard lock(connection->address_lock_);
      EXPECT_EQ(0u, connection->jit_memory_regions_.size());
    }
  }
};

TEST(TestConnection, MapUnmap) {
  TestConnection test;
  test.MapUnmap();
}

TEST(TestConnection, CommitMemory) {
  TestConnection test;
  test.CommitMemory();
}

TEST(TestConnection, CommitDecommitMemory) {
  TestConnection test;
  test.CommitDecommitMemory();
}

TEST(TestConnection, CommitLargeBuffer) {
  TestConnection test;
  test.CommitLargeBuffer();
}

TEST(TestConnection, Notification) {
  TestConnection test;
  test.Notification();
}

TEST(TestConnection, DestructionNotification) {
  TestConnection test;
  test.DestructionNotification();
}

TEST(TestConnection, SoftwareAtom) {
  TestConnection test;
  test.SoftwareAtom();
}

TEST(TestConnection, GrowableMemory) {
  TestConnection test;
  test.GrowableMemory();
}

TEST(TestConnection, FlushRegion) {
  TestConnection test;
  test.FlushRegion();
}

TEST(TestConnection, FlushUncachedRegion) {
  TestConnection test;
  test.FlushUncachedRegion();
}

TEST(TestConnection, PhysicalToVirtual) {
  TestConnection test;
  test.PhysicalToVirtual();
}

TEST(TestConnection, DeregisterConnection) {
  TestConnection test;
  test.DeregisterConnection();
}

TEST(TestConnection, ContextCount) {
  TestConnection test;
  test.ContextCount();
}

TEST(TestConnection, JitAddressSpaceAllocate) {
  TestConnection test;
  test.JitAddressSpaceAllocate();
}

TEST(TestConnection, JitParseAllocate) {
  TestConnection test;
  test.JitParseAllocate();
}

TEST(TestConnection, JitParseFree) {
  TestConnection test;
  test.JitParseFree();
}

TEST(TestConnection, JitAllocateNormal) {
  TestConnection test;
  test.JitAllocateNormal();
}

TEST(TestConnection, JitAllocateWriteCombining) {
  TestConnection test;
  test.JitAllocateWriteCombining();
}

TEST(TestConnection, JitAllocateReuseChoice) {
  TestConnection test;
  test.JitAllocateWriteCombining();
}

TEST(TestConnection, JitAllocateInvalidCommitSize) {
  TestConnection test;
  test.JitAllocateInvalidCommitSize();
}

TEST(TestConnection, JitAllocateInvalidWriteAddress) {
  TestConnection test;
  test.JitAllocateInvalidWriteAddress();
}

TEST(TestConnection, MemoryPressure) {
  TestConnection test;
  test.MemoryPressure();
}
