// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_MANAGER_H_
#define ADDRESS_MANAGER_H_

#include <mutex>
#include <vector>

#include "address_space.h"
#include "msd_arm_atom.h"
#include "msd_arm_connection.h"
#include "registers.h"

class AddressManager final : public AddressSpaceObserver {
public:
    class Owner {
    public:
        virtual RegisterIo* register_io() = 0;
    };

    AddressManager(Owner* owner, uint32_t address_slot_count);

    bool AssignAddressSpace(MsdArmAtom* atom);

    void AtomFinished(MsdArmAtom* atom);

    // AddressSpaceObserver implementation.
    void FlushAddressMappingRange(AddressSpace*, uint64_t start, uint64_t length) override;
    void ReleaseSpaceMappings(AddressSpace* address_space) override;

private:
    struct AddressSlot {
        std::weak_ptr<AddressSlotMapping> mapping;

        // This is the AddressSpace* that the slot is attached to.  Will be
        // set to null by AddressSpace destructor if this is attached to an
        // address space. This can't be a weak pointer because we need to
        // compare against it in the AddressSpace destructor.
        void* address_space = nullptr;
    };

    // Wait for the MMU to finish processing any existing commands.
    static void WaitForMmuIdle(RegisterIo* io, registers::AsRegisters as_regs);

    static void FlushMmuRange(RegisterIo* io, registers::AsRegisters as_regs, uint64_t start,
                              uint64_t length);

    std::shared_ptr<AddressSlotMapping>
    GetMappingForAddressSpaceUnlocked(AddressSpace* address_space);
    std::shared_ptr<AddressSlotMapping>
    AllocateMappingForAddressSpace(std::shared_ptr<MsdArmConnection> connection);
    std::shared_ptr<AddressSlotMapping> AssignToSlot(std::shared_ptr<MsdArmConnection> connection,
                                                     uint32_t slot);

    void InvalidateSlot(uint32_t slot);

    Owner* owner_;
    std::mutex address_slot_lock_;
    std::vector<AddressSlot> address_slots_;
};

#endif // ADDRESS_MANAGER_H_
