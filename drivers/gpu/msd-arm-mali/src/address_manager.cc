// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_manager.h"

#include <chrono>

#include "address_space.h"
#include "msd_arm_connection.h"

// Normal memory, outer non-cacheable, inner cacheable read+write alloc. The
// definition of this is similar to normal LPAE memory attributes, but is
// undocumented.
constexpr uint8_t kMmuNormalMemoryAttr = 0x4d;

constexpr uint8_t kMmuUnusedAttr = 0;

// The memory attribute register has 8 8-bit slots.
static constexpr uint64_t SlotAttribute(int slot, uint8_t attributes)
{
    return static_cast<uint64_t>(attributes) << (slot * 8);
}

// Only one type of memory is ever used, so that attribute is put in slot 0
// and slot 0 is referenced by all page table entries.
constexpr uint64_t kMemoryAttributes =
    SlotAttribute(0, kMmuNormalMemoryAttr) | SlotAttribute(1, kMmuUnusedAttr) |
    SlotAttribute(2, kMmuUnusedAttr) | SlotAttribute(3, kMmuUnusedAttr) |
    SlotAttribute(4, kMmuUnusedAttr) | SlotAttribute(5, kMmuUnusedAttr) |
    SlotAttribute(6, kMmuUnusedAttr) | SlotAttribute(7, kMmuUnusedAttr);

AddressManager::AddressManager(Owner* owner, uint32_t address_slot_count) : owner_(owner)
{
    address_slots_.resize(address_slot_count);
}

bool AddressManager::AssignAddressSpace(MsdArmAtom* atom)
{
    DASSERT(!atom->address_slot_mapping());
    auto connection = atom->connection().lock();
    if (!connection)
        return false;

    std::shared_ptr<AddressSlotMapping> mapping = AllocateMappingForAddressSpace(connection);
    atom->set_address_slot_mapping(mapping);

    return mapping ? true : false;
}

void AddressManager::AtomFinished(MsdArmAtom* atom)
{
    if (!atom->address_slot_mapping())
        return;
    atom->set_address_slot_mapping(nullptr);
}

std::shared_ptr<AddressSlotMapping>
AddressManager::GetMappingForAddressSpaceUnlocked(AddressSpace* address_space)
{
    DASSERT(address_space);
    for (size_t i = 0; i < address_slots_.size(); ++i) {
        AddressSlot& slot = address_slots_[i];
        if (slot.address_space != address_space)
            continue;
        auto mapping = slot.mapping.lock();
        if (!mapping) {
            mapping = std::make_shared<AddressSlotMapping>(
                i, std::static_pointer_cast<MsdArmConnection>(address_space->owner()));
            slot.mapping = mapping;
        }
        return mapping;
    }

    return nullptr;
}

void AddressManager::FlushAddressMappingRange(AddressSpace* address_space, uint64_t start,
                                              uint64_t length)
{
    std::shared_ptr<AddressSlotMapping> mapping;
    {
        std::lock_guard<std::mutex> lock(address_slot_lock_);
        mapping = AddressManager::GetMappingForAddressSpaceUnlocked(address_space);
    }
    if (!mapping)
        return;
    FlushMmuRange(owner_->register_io(), registers::AsRegisters(mapping->slot_number()), start,
                  length);
}

std::shared_ptr<AddressSlotMapping>
AddressManager::AllocateMappingForAddressSpace(std::shared_ptr<MsdArmConnection> connection)
{
    std::lock_guard<std::mutex> lock(address_slot_lock_);
    std::shared_ptr<AddressSlotMapping> mapping =
        GetMappingForAddressSpaceUnlocked(connection->address_space());
    if (mapping)
        return mapping;

    // Allocate new mapping (trying to avoid evicting).
    for (size_t i = 0; i < address_slots_.size(); ++i) {
        if (!address_slots_[i].address_space)
            return AssignToSlot(connection, i);
    }

    // TODO(MA-386): Evict the LRU slot.
    for (size_t i = 0; i < address_slots_.size(); ++i) {
        if (address_slots_[i].mapping.expired())
            return AssignToSlot(connection, i);
    }

    // TODO(MA-386): Wait until an address space is free.
    return DRETP(nullptr, "All address slots in use");
}

std::shared_ptr<AddressSlotMapping>
AddressManager::AssignToSlot(std::shared_ptr<MsdArmConnection> connection, uint32_t slot_number)
{
    DLOG("Assigning connection %p to slot %d\n", connection.get(), slot_number);
    AddressSlot& slot = address_slots_[slot_number];
    auto old_address_space = slot.address_space;
    if (old_address_space)
        InvalidateSlot(slot_number);
    auto mapping = std::make_shared<AddressSlotMapping>(slot_number, connection);
    slot.mapping = mapping;
    slot.address_space = connection->address_space();

    registers::AsRegisters as_reg(slot_number);
    RegisterIo* io = owner_->register_io();
    WaitForMmuIdle(io, as_reg);

    uint64_t translation_table_entry = connection->address_space()->translation_table_entry();
    as_reg.TranslationTable().FromValue(translation_table_entry).WriteTo(io);

    as_reg.MemoryAttributes().FromValue(kMemoryAttributes).WriteTo(io);

    as_reg.Command().FromValue(registers::AsCommand::kCmdUpdate).WriteTo(io);
    return mapping;
}

void AddressManager::ReleaseSpaceMappings(AddressSpace* address_space)
{
    std::lock_guard<std::mutex> lock(address_slot_lock_);
    for (size_t i = 0; i < address_slots_.size(); ++i) {
        AddressSlot& slot = address_slots_[i];
        if (slot.address_space != address_space)
            continue;
        DASSERT(slot.mapping.expired());
        InvalidateSlot(i);
        slot.address_space = nullptr;
    }
}

void AddressManager::InvalidateSlot(uint32_t slot)
{
    RegisterIo* io = owner_->register_io();
    registers::AsRegisters as_reg(slot);
    WaitForMmuIdle(io, as_reg);
    constexpr uint64_t kFullAddressSpaceSize = 1ul << AddressSpace::kVirtualAddressSize;
    FlushMmuRange(io, as_reg, 0, kFullAddressSpaceSize);

    as_reg.TranslationTable().FromValue(0).WriteTo(io);
    as_reg.MemoryAttributes().FromValue(kMemoryAttributes).WriteTo(io);

    as_reg.Command().FromValue(registers::AsCommand::kCmdUpdate).WriteTo(io);
}

// static
void AddressManager::WaitForMmuIdle(RegisterIo* io, registers::AsRegisters as_regs)
{
    auto status_reg = as_regs.Status();
    if (!status_reg.ReadFrom(io).reg_value())
        return;

    auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (status_reg.ReadFrom(io).reg_value() && std::chrono::steady_clock::now() < timeout)
        ;

    uint32_t status = status_reg.ReadFrom(io).reg_value();
    if (status)
        magma::log(magma::LOG_WARNING, "Wait for MMU %d to idle timed out with status 0x%x\n",
                   as_regs.address_space(), status);
}

void AddressManager::FlushMmuRange(RegisterIo* io, registers::AsRegisters as_regs, uint64_t start,
                                   uint64_t length)
{
    DASSERT(magma::is_page_aligned(start));
    uint64_t region = start;
    uint64_t num_pages = length >> PAGE_SHIFT;
    uint8_t log2_num_pages = 0;
    if (num_pages > 0) {
        log2_num_pages = 63 - __builtin_clzl(num_pages);
        if ((1ul << log2_num_pages) < num_pages)
            log2_num_pages++;
    }

    constexpr uint32_t kRegionLengthOffset = 11;

    // The low 12 bits are used to specify how many pages are to be locked in
    // this operation.
    static_assert(kRegionLengthOffset + 64 < PAGE_SIZE, "maximum region length is too large");

    uint8_t region_width = log2_num_pages + kRegionLengthOffset;

    region |= region_width;
    as_regs.LockAddress().FromValue(region).WriteTo(io);
    as_regs.Command().FromValue(registers::AsCommand::kCmdLock).WriteTo(io);
    WaitForMmuIdle(io, as_regs);
    // Both invalidate the TLB entries and throw away data in the L2 cache
    // corresponding to them, or otherwise the cache may be written back to
    // memory after the memory's started being used for something else.
    as_regs.Command().FromValue(registers::AsCommand::kCmdFlushMem).WriteTo(io);
    WaitForMmuIdle(io, as_regs);
}
