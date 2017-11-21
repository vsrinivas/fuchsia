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
// Memory with this attribute is also outer cacheable read+write alloc.
constexpr uint8_t kMmuOuterCacheableMemoryAttr = 0x8d;

// The memory attribute register has 8 8-bit slots.
static constexpr uint64_t SlotAttribute(int slot, uint8_t attributes)
{
    return static_cast<uint64_t>(attributes) << (slot * 8);
}

constexpr uint64_t kMemoryAttributes =
    SlotAttribute(AddressSpace::kNormalMemoryAttributeSlot, kMmuNormalMemoryAttr) |
    SlotAttribute(AddressSpace::kOuterCacheableAttributeSlot, kMmuOuterCacheableMemoryAttr);

AddressManager::AddressManager(Owner* owner, uint32_t address_slot_count) : owner_(owner)
{
    address_slots_.resize(address_slot_count);
    for (uint32_t i = 0; i < address_slot_count; i++) {
        registers_.push_back(std::make_unique<HardwareSlot>(i));
    }
}

bool AddressManager::AssignAddressSpace(MsdArmAtom* atom)
{
    DASSERT(!atom->address_slot_mapping());
    auto connection = atom->connection().lock();
    if (!connection)
        return false;
    if (connection->address_space_lost())
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

std::shared_ptr<AddressSlotMapping> AddressManager::GetMappingForSlot(uint32_t slot_number)
{
    std::lock_guard<std::mutex> lock(address_slot_lock_);
    auto& slot = address_slots_[slot_number];
    return slot.mapping.lock();
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
    HardwareSlot* slot;
    std::shared_ptr<AddressSlotMapping> mapping;
    {
        std::lock_guard<std::mutex> lock(address_slot_lock_);
        mapping = AddressManager::GetMappingForAddressSpaceUnlocked(address_space);
        if (!mapping)
            return;
        slot = registers_[mapping->slot_number()].get();
        // Grab the hardware lock inside the address slot lock so we can be
        // sure the address slot still maps to the same address space.
        // std::unique_lock can't be used because it interacts poorly with
        // thread-safety analysis
        slot->lock.lock();
    }
    slot->FlushMmuRange(owner_->register_io(), start, length);

    // The mapping will be released before the hardware lock, so that we
    // can be sure that the mapping will be expired after ReleaseSpaceMappings
    // acquires the hardware lock.
    slot->lock.unlock();
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
    HardwareSlot& hardware_slot = *registers_[slot_number];
    std::lock_guard<std::mutex> lock(hardware_slot.lock);

    auto old_address_space = slot.address_space;
    RegisterIo* io = owner_->register_io();
    if (old_address_space)
        hardware_slot.InvalidateSlot(io);
    auto mapping = std::make_shared<AddressSlotMapping>(slot_number, connection);
    slot.mapping = mapping;
    slot.address_space = connection->address_space();

    registers::AsRegisters& as_reg = hardware_slot.registers;
    hardware_slot.WaitForMmuIdle(io);

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
        // Grab lock to ensure the registers aren't being modified during
        // invalidate.
        HardwareSlot& hardware_slot = *registers_[i];
        std::lock_guard<std::mutex> lock(hardware_slot.lock);
        DASSERT(slot.mapping.expired());
        hardware_slot.InvalidateSlot(owner_->register_io());
        slot.address_space = nullptr;
    }
}

void AddressManager::HardwareSlot::InvalidateSlot(RegisterIo* io)
{
    WaitForMmuIdle(io);
    constexpr uint64_t kFullAddressSpaceSize = 1ul << AddressSpace::kVirtualAddressSize;
    FlushMmuRange(io, 0, kFullAddressSpaceSize);

    registers.TranslationTable().FromValue(0).WriteTo(io);
    registers.MemoryAttributes().FromValue(kMemoryAttributes).WriteTo(io);

    registers.Command().FromValue(registers::AsCommand::kCmdUpdate).WriteTo(io);
}

void AddressManager::HardwareSlot::WaitForMmuIdle(RegisterIo* io)
{
    auto status_reg = registers.Status();
    if (!status_reg.ReadFrom(io).reg_value())
        return;

    auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (status_reg.ReadFrom(io).reg_value() && std::chrono::steady_clock::now() < timeout)
        ;

    uint32_t status = status_reg.ReadFrom(io).reg_value();
    if (status)
        magma::log(magma::LOG_WARNING, "Wait for MMU %d to idle timed out with status 0x%x\n",
                   registers.address_space(), status);
}

void AddressManager::HardwareSlot::FlushMmuRange(RegisterIo* io, uint64_t start, uint64_t length)
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
    registers.LockAddress().FromValue(region).WriteTo(io);
    registers.Command().FromValue(registers::AsCommand::kCmdLock).WriteTo(io);
    WaitForMmuIdle(io);
    // Both invalidate the TLB entries and throw away data in the L2 cache
    // corresponding to them, or otherwise the cache may be written back to
    // memory after the memory's started being used for something else.
    registers.Command().FromValue(registers::AsCommand::kCmdFlushMem).WriteTo(io);
    WaitForMmuIdle(io);
}
