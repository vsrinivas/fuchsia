// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_MANAGER_H_
#define ADDRESS_MANAGER_H_

#include <condition_variable>
#include <mutex>
#include <vector>

#include "lib/fxl/synchronization/thread_annotations.h"

#include "address_space.h"
#include "msd_arm_atom.h"
#include "msd_arm_connection.h"
#include "registers.h"

// The address manager can be modified by the device thread (to assign and
// unassign address spaces from registers before mapping and unmapping them)
// and by the connection thread that owns an address space, to ensure that
// the page mappings are flushed properly.
class AddressManager final : public AddressSpaceObserver {
public:
    class Owner {
    public:
        virtual magma::RegisterIo* register_io() = 0;
    };

    AddressManager(Owner* owner, uint32_t address_slot_count);

    bool AssignAddressSpace(MsdArmAtom* atom);

    void AtomFinished(MsdArmAtom* atom);

    std::shared_ptr<AddressSlotMapping> GetMappingForSlot(uint32_t slot);

    std::shared_ptr<AddressSlotMapping>
    AllocateMappingForAddressSpace(std::shared_ptr<MsdArmConnection> connection);

    // AddressSpaceObserver implementation.
    void FlushAddressMappingRange(AddressSpace*, uint64_t start, uint64_t length,
                                  bool synchronous) override;
    void ReleaseSpaceMappings(const AddressSpace* address_space) override;

    void UnlockAddressSpace(AddressSpace*) override;

    void set_acquire_slot_timeout_seconds(uint32_t timeout)
    {
        acquire_slot_timeout_seconds_ = timeout;
    }

private:
    struct AddressSlot {
        std::weak_ptr<AddressSlotMapping> mapping;

        // This is the AddressSpace* that the slot is attached to.  Will be
        // set to null by AddressSpace destructor if this is attached to an
        // address space. This can't be a weak pointer because we need to
        // compare against it in the AddressSpace destructor.
        const void* address_space = nullptr;
    };

    struct HardwareSlot {
        HardwareSlot(uint32_t slot) : registers(slot) {}

        void FlushMmuRange(magma::RegisterIo* io, uint64_t start, uint64_t length, bool synchronous)
            FXL_EXCLUSIVE_LOCKS_REQUIRED(lock);
        // Wait for the MMU to finish processing any existing commands.
        void WaitForMmuIdle(magma::RegisterIo* io) FXL_EXCLUSIVE_LOCKS_REQUIRED(lock);
        void InvalidateSlot(magma::RegisterIo* io) FXL_EXCLUSIVE_LOCKS_REQUIRED(lock);
        void UnlockMmu(magma::RegisterIo* io) FXL_EXCLUSIVE_LOCKS_REQUIRED(lock);

        std::mutex lock;
        FXL_GUARDED_BY(lock) registers::AsRegisters registers;
    };

    std::shared_ptr<AddressSlotMapping>
    GetMappingForAddressSpaceUnlocked(const AddressSpace* address_space)
        FXL_EXCLUSIVE_LOCKS_REQUIRED(address_slot_lock_);
    std::shared_ptr<AddressSlotMapping> AssignToSlot(std::shared_ptr<MsdArmConnection> connection,
                                                     uint32_t slot)
        FXL_EXCLUSIVE_LOCKS_REQUIRED(address_slot_lock_);

    Owner* owner_;
    uint32_t acquire_slot_timeout_seconds_ = 10;
    std::mutex address_slot_lock_;
    FXL_GUARDED_BY(address_slot_lock_) std::vector<AddressSlot> address_slots_;
    std::condition_variable address_slot_free_;

    // Before a slot is modified, the corresponding lock should be taken.
    // It should only be taken while address_slot_lock_ is
    // locked.
    std::vector<std::unique_ptr<HardwareSlot>> registers_;
};

#endif // ADDRESS_MANAGER_H_
