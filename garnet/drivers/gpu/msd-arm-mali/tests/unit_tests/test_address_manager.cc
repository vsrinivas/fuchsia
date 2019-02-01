// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <future>

#include "address_manager.h"
#include "mock/mock_bus_mapper.h"
#include "mock/mock_mmio.h"
#include "platform_mmio.h"
#include "registers.h"
#include "gtest/gtest.h"

namespace {

class FakeOwner : public AddressManager::Owner {
public:
    FakeOwner(magma::RegisterIo* regs) : register_io_(regs) {}

    magma::RegisterIo* register_io() override { return register_io_; }

private:
    magma::RegisterIo* register_io_;
};

class TestConnectionOwner : public MsdArmConnection::Owner {
public:
    TestConnectionOwner(AddressManager* manager) : manager_(manager) {}

    void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override {}
    void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) override {}
    AddressSpaceObserver* GetAddressSpaceObserver() override { return manager_; }
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

private:
    AddressManager* manager_;
    MockBusMapper bus_mapper_;
};

static constexpr uint64_t kMemoryAttributes = 0x8848u;

TEST(AddressManager, MultipleAtoms)
{
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    FakeOwner owner(reg_io.get());
    AddressManager address_manager(&owner, 8);
    TestConnectionOwner connection_owner(&address_manager);
    std::shared_ptr<MsdArmConnection> connection1 = MsdArmConnection::Create(0, &connection_owner);
    auto atom1 = std::make_unique<MsdArmAtom>(connection1, 0, 0, 0, magma_arm_mali_user_data(), 0);

    EXPECT_TRUE(address_manager.AssignAddressSpace(atom1.get()));

    std::shared_ptr<MsdArmConnection> connection2 = MsdArmConnection::Create(0, &connection_owner);
    auto atom2 = std::make_unique<MsdArmAtom>(connection2, 0, 0, 0, magma_arm_mali_user_data(), 0);
    EXPECT_TRUE(address_manager.AssignAddressSpace(atom2.get()));

    EXPECT_EQ(0u, atom1->address_slot_mapping()->slot_number());
    EXPECT_EQ(1u, atom2->address_slot_mapping()->slot_number());

    registers::AsRegisters as_regs(0);
    EXPECT_EQ(kMemoryAttributes, as_regs.MemoryAttributes().ReadFrom(reg_io.get()).reg_value());
    uint64_t translation_table_entry1 =
        connection1->const_address_space()->translation_table_entry();
    EXPECT_EQ(translation_table_entry1,
              as_regs.TranslationTable().ReadFrom(reg_io.get()).reg_value());

    registers::AsRegisters as_regs1(1);
    EXPECT_EQ(kMemoryAttributes, as_regs1.MemoryAttributes().ReadFrom(reg_io.get()).reg_value());
    EXPECT_EQ(connection2->const_address_space()->translation_table_entry(),
              as_regs1.TranslationTable().ReadFrom(reg_io.get()).reg_value());

    connection1.reset();
    // atom1 should hold a reference to the translation table entry.
    EXPECT_EQ(translation_table_entry1,
              as_regs.TranslationTable().ReadFrom(reg_io.get()).reg_value());

    address_manager.AtomFinished(atom1.get());
    EXPECT_EQ(kMemoryAttributes, as_regs.MemoryAttributes().ReadFrom(reg_io.get()).reg_value());
    EXPECT_EQ(0u, as_regs.TranslationTable().ReadFrom(reg_io.get()).reg_value() & 0xff);

    EXPECT_FALSE(address_manager.AssignAddressSpace(atom1.get()));

    address_manager.AtomFinished(atom2.get());

    auto atom3 = std::make_unique<MsdArmAtom>(connection2, 0, 0, 0, magma_arm_mali_user_data(), 0);
    EXPECT_TRUE(address_manager.AssignAddressSpace(atom3.get()));
    EXPECT_EQ(1u, atom3->address_slot_mapping()->slot_number());
}

TEST(AddressManager, PreferUnused)
{
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    FakeOwner owner(reg_io.get());
    AddressManager address_manager(&owner, 8);
    TestConnectionOwner connection_owner(&address_manager);
    std::shared_ptr<MsdArmConnection> connection1 = MsdArmConnection::Create(0, &connection_owner);
    auto atom1 = std::make_unique<MsdArmAtom>(connection1, 0, 0, 0, magma_arm_mali_user_data(), 0);

    EXPECT_TRUE(address_manager.AssignAddressSpace(atom1.get()));
    EXPECT_EQ(0u, atom1->address_slot_mapping()->slot_number());
    address_manager.AtomFinished(atom1.get());

    std::shared_ptr<MsdArmConnection> connection2 = MsdArmConnection::Create(0, &connection_owner);
    auto atom2 = std::make_unique<MsdArmAtom>(connection2, 0, 0, 0, magma_arm_mali_user_data(), 0);
    EXPECT_TRUE(address_manager.AssignAddressSpace(atom2.get()));

    // Slots that are mapped to connections should only be reused if empty
    // slots are not available.
    EXPECT_EQ(1u, atom2->address_slot_mapping()->slot_number());
}

TEST(AddressManager, ReuseSlot)
{
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    FakeOwner owner(reg_io.get());

    const uint32_t kNumberAddressSpaces = 8;
    AddressManager address_manager(&owner, kNumberAddressSpaces);
    TestConnectionOwner connection_owner(&address_manager);

    std::vector<std::shared_ptr<MsdArmConnection>> connections;
    std::vector<std::unique_ptr<MsdArmAtom>> atoms;
    for (size_t i = 0; i < kNumberAddressSpaces; i++) {
        connections.push_back(MsdArmConnection::Create(0, &connection_owner));
        atoms.push_back(std::make_unique<MsdArmAtom>(connections.back(), 0, 0, 0,
                                                     magma_arm_mali_user_data(), 0));
        EXPECT_TRUE(address_manager.AssignAddressSpace(atoms.back().get()));
    }

    registers::AsRegisters as_regs(2);
    EXPECT_EQ(kMemoryAttributes, as_regs.MemoryAttributes().ReadFrom(reg_io.get()).reg_value());
    uint64_t translation_table_entry =
        connections[2]->const_address_space()->translation_table_entry();
    EXPECT_EQ(translation_table_entry,
              as_regs.TranslationTable().ReadFrom(reg_io.get()).reg_value());

    connections.push_back(MsdArmConnection::Create(0, &connection_owner));
    atoms.push_back(
        std::make_unique<MsdArmAtom>(connections.back(), 0, 0, 0, magma_arm_mali_user_data(), 0));
    // Reduce timeout to make test faster.
    address_manager.set_acquire_slot_timeout_seconds(1);
    EXPECT_FALSE(address_manager.AssignAddressSpace(atoms.back().get()));
    address_manager.set_acquire_slot_timeout_seconds(10);

    auto future = std::async(std::launch::async, [&]() {
        // Sleep to try to ensure AssignAddressSpace is currently running.
        usleep(10000);
        address_manager.AtomFinished(atoms[2].get());
    });

    EXPECT_TRUE(address_manager.AssignAddressSpace(atoms.back().get()));

    uint64_t new_translation_table_entry =
        connections[8]->const_address_space()->translation_table_entry();
    EXPECT_EQ(new_translation_table_entry,
              as_regs.TranslationTable().ReadFrom(reg_io.get()).reg_value());
}

TEST(AddressManager, FlushAddressRange)
{
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    FakeOwner owner(reg_io.get());
    auto mapper = std::unique_ptr<MockBusMapper>();

    const uint32_t kNumberAddressSpaces = 8;
    AddressManager address_manager(&owner, kNumberAddressSpaces);
    TestConnectionOwner connection_owner(&address_manager);
    std::shared_ptr<MsdArmConnection> connection = MsdArmConnection::Create(0, &connection_owner);

    auto atom = std::make_unique<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data(), 0);
    EXPECT_TRUE(address_manager.AssignAddressSpace(atom.get()));

    uint64_t addr = PAGE_SIZE * 0xbdefcccef;
    std::unique_ptr<magma::PlatformBuffer> buffer;

    buffer = magma::PlatformBuffer::Create(PAGE_SIZE * 3, "test");

    auto bus_mapping = connection_owner.GetBusMapper()->MapPageRangeBus(buffer.get(), 0,
                                                                        buffer->size() / PAGE_SIZE);
    ASSERT_NE(nullptr, bus_mapping);

    EXPECT_TRUE(connection->address_space_for_testing()->Insert(
        addr, bus_mapping.get(), 0, buffer->size(), kAccessFlagRead | kAccessFlagNoExecute));
    // 3 pages should be cleared, so it should be rounded up to 4 (and log
    // base 2 is 2).
    constexpr uint64_t kLockOffset = 13;
    registers::AsRegisters as_regs(0);
    EXPECT_EQ(addr | kLockOffset, as_regs.LockAddress().ReadFrom(reg_io.get()).reg_value());
    EXPECT_EQ(registers::AsCommand::kCmdFlushPageTable,
              as_regs.Command().ReadFrom(reg_io.get()).reg_value());

    EXPECT_TRUE(connection->address_space_for_testing()->Clear(addr, buffer->size()));

    EXPECT_EQ(addr | kLockOffset, as_regs.LockAddress().ReadFrom(reg_io.get()).reg_value());
    EXPECT_EQ(registers::AsCommand::kCmdFlushMem,
              as_regs.Command().ReadFrom(reg_io.get()).reg_value());
    address_manager.AtomFinished(atom.get());
    connection.reset();

    // Clear entire address range.
    EXPECT_EQ(10u + (48 - PAGE_SHIFT) + 1,
              as_regs.LockAddress().ReadFrom(reg_io.get()).reg_value());
    EXPECT_EQ(registers::AsCommand::kCmdUpdate,
              as_regs.Command().ReadFrom(reg_io.get()).reg_value());
}
} // namespace
