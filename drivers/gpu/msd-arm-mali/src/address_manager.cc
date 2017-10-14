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

bool AddressManager::AssignAddressSpace(RegisterIo* io, MsdArmAtom* atom)
{
    DASSERT(!connection_);
    connection_ = atom->connection().lock();
    if (!connection_)
        return false;

    // Always use address space 0 for now.
    registers::AsRegisters as_reg(0);
    WaitForMmuIdle(io, as_reg);

    uint64_t translation_table_entry = connection_->address_space()->translation_table_entry();
    as_reg.TranslationTable().FromValue(translation_table_entry).WriteTo(io);

    as_reg.MemoryAttributes().FromValue(kMemoryAttributes).WriteTo(io);

    as_reg.Command().FromValue(registers::AsCommand::kCmdUpdate).WriteTo(io);
    return true;
}

void AddressManager::AtomFinished(RegisterIo* io, MsdArmAtom* atom)
{
    if (!connection_)
        return;
    connection_.reset();
    // Detach the MMU from the address space, so it hopefully picks up any
    // changes to the address space mapping for later atoms.
    // TODO(MA-363): Flush the MMU range when an address mapping changes.

    registers::AsRegisters as_reg(0);
    WaitForMmuIdle(io, as_reg);

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
