// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "mock/mock_bus_mapper.h"
#include "gtest/gtest.h"

#include "address_manager.h"
#include "gpu_mapping.h"
#include "msd_arm_buffer.h"
#include "msd_arm_connection.h"

namespace {

class TestAddressSpaceObserver : public AddressSpaceObserver {
public:
    void FlushAddressMappingRange(AddressSpace*, uint64_t start, uint64_t length,
                                  bool synchronous) override
    {
    }
    void UnlockAddressSpace(AddressSpace* address_space) override
    {
        unlocked_address_spaces_.push_back(address_space);
    }
    void ReleaseSpaceMappings(const AddressSpace* address_space) override {}

    const std::vector<AddressSpace*>& unlocked_address_spaces() const
    {
        return unlocked_address_spaces_;
    }

private:
    std::vector<AddressSpace*> unlocked_address_spaces_;
};

class FakeConnectionOwner : public MsdArmConnection::Owner {
public:
    FakeConnectionOwner() {}

    void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override { atoms_list_.push_back(atom); }
    void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) override
    {
        cancel_atoms_list_.push_back(connection.get());
    }
    AddressSpaceObserver* GetAddressSpaceObserver() override { return &observer_; }
    TestAddressSpaceObserver* GetTestAddressSpaceObserver() { return &observer_; }
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

    const std::vector<MsdArmConnection*>& cancel_atoms_list() { return cancel_atoms_list_; }
    const std::vector<std::shared_ptr<MsdArmAtom>>& atoms_list() { return atoms_list_; }

private:
    TestAddressSpaceObserver observer_;
    MockBusMapper bus_mapper_;
    std::vector<MsdArmConnection*> cancel_atoms_list_;
    std::vector<std::shared_ptr<MsdArmAtom>> atoms_list_;
};

void* g_test_token;
uint32_t g_test_data_size;
magma_arm_mali_status g_status;

void TestCallback(void* token, msd_notification_t* notification)
{
    g_test_token = token;
    if (notification->type == MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND) {
        g_test_data_size = notification->u.channel_send.size;
        memcpy(&g_status, notification->u.channel_send.data, g_test_data_size);
    }
}
} // namespace

class TestConnection {
public:
    void MapUnmap()
    {
        FakeConnectionOwner owner;
        auto connection = MsdArmConnection::Create(0, &owner);
        EXPECT_TRUE(connection);
        constexpr uint64_t kBufferSize = PAGE_SIZE * 100;

        std::shared_ptr<MsdArmBuffer> buffer(
            MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
        EXPECT_TRUE(buffer);

        // GPU VA not page aligned
        EXPECT_FALSE(connection->AddMapping(
            std::make_unique<GpuMapping>(1, 0, 1, 0, connection.get(), buffer)));

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

    void CommitMemory()
    {
        FakeConnectionOwner owner;
        auto connection = MsdArmConnection::Create(0, &owner);
        EXPECT_TRUE(connection);
        constexpr uint64_t kBufferSize = PAGE_SIZE * 100;
        AddressSpace* address_space = connection->address_space_for_testing();

        std::shared_ptr<MsdArmBuffer> buffer(
            MsdArmBuffer::Create(kBufferSize, "test-buffer").release());
        EXPECT_TRUE(buffer);

        constexpr uint64_t kGpuOffset[] = {1000, 1100};

        EXPECT_TRUE(connection->AddMapping(std::make_unique<GpuMapping>(
            kGpuOffset[0] * PAGE_SIZE, 1, PAGE_SIZE * 99, 0, connection.get(), buffer)));

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

        EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[1] + 1) * PAGE_SIZE, &pte));
        EXPECT_NE(kInvalidPte, pte);
        // The mapping should be truncated because it's only for 2 pages.
        EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[1] + 2) * PAGE_SIZE, &pte));
        EXPECT_EQ(kInvalidPte, pte);
        EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
        EXPECT_NE(kInvalidPte, pte);

        EXPECT_TRUE(connection->RemoveMapping(kGpuOffset[1] * PAGE_SIZE));

        // Should unmap the last page.
        EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 4));
        EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
        EXPECT_EQ(kInvalidPte, pte);

        // Should be ignored because offset isn't supported.
        EXPECT_FALSE(connection->CommitMemoryForBuffer(buffer.get(), 0, 6));
        EXPECT_TRUE(address_space->ReadPteForTesting((kGpuOffset[0] + 4) * PAGE_SIZE, &pte));
        EXPECT_EQ(kInvalidPte, pte);

        // Can decommit entire buffer.
        EXPECT_TRUE(connection->CommitMemoryForBuffer(buffer.get(), 1, 0));
        EXPECT_FALSE(address_space->ReadPteForTesting(kGpuOffset[0] * PAGE_SIZE, &pte));
    }

    void CommitLargeBuffer()
    {
        FakeConnectionOwner owner;
        auto connection = MsdArmConnection::Create(0, &owner);
        EXPECT_TRUE(connection);
        constexpr uint64_t kBufferSize = 1ul << 35; // 32 GB

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

    void GrowableMemory()
    {
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

    void Notification()
    {
        FakeConnectionOwner owner;
        auto connection = MsdArmConnection::Create(0, &owner);
        EXPECT_TRUE(connection);
        MsdArmAtom atom(connection, 0, 1, 5, magma_arm_mali_user_data{7, 8}, 0);

        // Shouldn't do anything.
        connection->SendNotificationData(&atom, static_cast<ArmMaliResultCode>(10));

        uint32_t token;
        connection->SetNotificationCallback(&TestCallback, &token);
        connection->SendNotificationData(&atom, static_cast<ArmMaliResultCode>(20));
        EXPECT_EQ(sizeof(g_status), g_test_data_size);
        EXPECT_EQ(&token, g_test_token);

        EXPECT_EQ(7u, g_status.data.data[0]);
        EXPECT_EQ(8u, g_status.data.data[1]);
        EXPECT_EQ(20u, g_status.result_code);
        EXPECT_EQ(5u, g_status.atom_number);

        connection->SetNotificationCallback(nullptr, nullptr);
        connection->SendNotificationData(&atom, static_cast<ArmMaliResultCode>(20));

        EXPECT_EQ(20u, g_status.result_code);
    }

    void DestructionNotification()
    {
        FakeConnectionOwner owner;
        auto connection = MsdArmConnection::Create(0, &owner);
        EXPECT_TRUE(connection);

        uint32_t token;
        connection->SetNotificationCallback(&TestCallback, &token);
        connection->MarkDestroyed();

        EXPECT_EQ(sizeof(g_status), g_test_data_size);
        EXPECT_EQ(&token, g_test_token);

        EXPECT_EQ(0u, g_status.data.data[0]);
        EXPECT_EQ(0u, g_status.data.data[1]);
        EXPECT_EQ(0u, g_status.atom_number);
        EXPECT_EQ(kArmMaliResultTerminated, g_status.result_code);

        // Shouldn't do anything.
        MsdArmAtom atom(connection, 0, 1, 5, magma_arm_mali_user_data{7, 8}, 0);
        connection->SendNotificationData(&atom, static_cast<ArmMaliResultCode>(10));
        EXPECT_EQ(kArmMaliResultTerminated, g_status.result_code);

        connection->SetNotificationCallback(nullptr, 0);

        EXPECT_EQ(1u, owner.cancel_atoms_list().size());
        EXPECT_EQ(connection.get(), owner.cancel_atoms_list()[0]);
    }

    void SoftwareAtom()
    {
        FakeConnectionOwner owner;
        auto connection = MsdArmConnection::Create(0, &owner);
        EXPECT_TRUE(connection);

        magma_arm_mali_atom client_atom = {};
        client_atom.flags = kAtomFlagSemaphoreWait;
        std::deque<std::shared_ptr<magma::PlatformSemaphore>> semaphores;
        EXPECT_FALSE(connection->ExecuteAtom(&client_atom, &semaphores));

        std::shared_ptr<magma::PlatformSemaphore> semaphore(magma::PlatformSemaphore::Create());
        semaphores.push_back(semaphore);
        EXPECT_TRUE(connection->ExecuteAtom(&client_atom, &semaphores));

        EXPECT_EQ(1u, owner.atoms_list().size());
        std::shared_ptr<MsdArmAtom> atom = owner.atoms_list()[0];
        std::shared_ptr<MsdArmSoftAtom> soft_atom = MsdArmSoftAtom::cast(atom);
        EXPECT_TRUE(!!soft_atom);
        EXPECT_EQ(kAtomFlagSemaphoreWait, soft_atom->soft_flags());
        EXPECT_EQ(semaphore, soft_atom->platform_semaphore());
    }
};

TEST(TestConnection, MapUnmap)
{
    TestConnection test;
    test.MapUnmap();
}

TEST(TestConnection, CommitMemory)
{
    TestConnection test;
    test.CommitMemory();
}

TEST(TestConnection, CommitLargeBuffer)
{
    TestConnection test;
    test.CommitLargeBuffer();
}

TEST(TestConnection, Notification)
{
    TestConnection test;
    test.Notification();
}

TEST(TestConnection, DestructionNotification)
{
    TestConnection test;
    test.DestructionNotification();
}

TEST(TestConnection, SoftwareAtom)
{
    TestConnection test;
    test.SoftwareAtom();
}

TEST(TestConnection, GrowableMemory)
{
    TestConnection test;
    test.GrowableMemory();
}
