// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_ATOM_H_
#define MSD_ARM_ATOM_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "address_space.h"
#include "magma_arm_mali_types.h"
#include "platform_semaphore.h"

class MsdArmAtom {
public:
    using DependencyList = std::vector<std::weak_ptr<MsdArmAtom>>;

    static constexpr uint64_t kInvalidGpuAddress = ~0ul;

    virtual ~MsdArmAtom() {}
    MsdArmAtom(std::weak_ptr<MsdArmConnection> connection, uint64_t gpu_address, uint32_t slot,
               uint8_t atom_number, magma_arm_mali_user_data user_data)
        : connection_(connection), gpu_address_(gpu_address), slot_(slot),
          atom_number_(atom_number), user_data_(user_data)
    {
    }

    std::weak_ptr<MsdArmConnection> connection() const { return connection_; }
    uint64_t gpu_address() const { return gpu_address_; }
    uint32_t slot() const { return slot_; }
    uint8_t atom_number() const { return atom_number_; }
    const magma_arm_mali_user_data& user_data() const { return user_data_; }

    void set_dependencies(const DependencyList& dependencies);
    bool AreDependenciesFinished();

    // These methods should only be called on the device thread.
    bool finished() const { return finished_; }
    void set_finished() { finished_ = true; }
    bool hard_stopped() const { return hard_stopped_; }
    void set_hard_stopped() { hard_stopped_ = true; }
    void SetExecutionStarted();

    std::chrono::time_point<std::chrono::steady_clock> execution_start_time() const
    {
        return execution_start_time_;
    }

    // These methods should only be called on the device thread.
    void set_address_slot_mapping(std::shared_ptr<AddressSlotMapping> address_slot_mapping);
    std::shared_ptr<AddressSlotMapping> address_slot_mapping() const
    {
        return address_slot_mapping_;
    }

    virtual bool is_soft_atom() const { return false; }

private:
    // The following data is immmutable after construction.
    const std::weak_ptr<MsdArmConnection> connection_;
    const uint64_t gpu_address_;
    const uint32_t slot_;
    DependencyList dependencies_;
    // Assigned by client.
    const uint8_t atom_number_;
    const magma_arm_mali_user_data user_data_;

    // This data is mutable after construction from the device thread.
    bool finished_ = false;
    std::shared_ptr<AddressSlotMapping> address_slot_mapping_;
    std::chrono::time_point<std::chrono::steady_clock> execution_start_time_;
    bool hard_stopped_ = false;
};

// Soft atoms don't actually execute in hardware.
class MsdArmSoftAtom : public MsdArmAtom {
public:
    static std::shared_ptr<MsdArmSoftAtom> cast(std::shared_ptr<MsdArmAtom> atom)
    {
        if (atom->is_soft_atom())
            return std::static_pointer_cast<MsdArmSoftAtom>(atom);
        return nullptr;
    }

    MsdArmSoftAtom(std::weak_ptr<MsdArmConnection> connection, AtomFlags soft_flags,
                   std::shared_ptr<magma::PlatformSemaphore> platform_semaphore,
                   uint8_t atom_number, magma_arm_mali_user_data user_data)
        : MsdArmAtom(connection, kInvalidGpuAddress, 0, atom_number, user_data),
          soft_flags_(soft_flags), platform_semaphore_(platform_semaphore)
    {
    }

    AtomFlags soft_flags() const { return soft_flags_; }
    std::shared_ptr<magma::PlatformSemaphore> platform_semaphore() const
    {
        return platform_semaphore_;
    }

    bool is_soft_atom() const override { return true; }

private:
    // Immutable after construction.
    const AtomFlags soft_flags_{};
    const std::shared_ptr<magma::PlatformSemaphore> platform_semaphore_;
};

#endif // MSD_ARM_ATOM_H_
