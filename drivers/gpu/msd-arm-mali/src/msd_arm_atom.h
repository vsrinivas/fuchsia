// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_ATOM_H_
#define MSD_ARM_ATOM_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "address_space.h"
#include "magma_arm_mali_types.h"

class MsdArmAtom {
public:
    using DependencyList = std::vector<std::weak_ptr<MsdArmAtom>>;

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

    // These methods should only be called on the device thread.
    void set_address_slot_mapping(std::shared_ptr<AddressSlotMapping> address_slot_mapping);
    std::shared_ptr<AddressSlotMapping> address_slot_mapping() const
    {
        return address_slot_mapping_;
    }

private:
    // The following data is immmutable after construction.
    std::weak_ptr<MsdArmConnection> connection_;
    uint64_t gpu_address_;
    uint32_t slot_;
    DependencyList dependencies_;
    // Assigned by client.
    uint8_t atom_number_;
    magma_arm_mali_user_data user_data_;

    // This data is mutable after construction from the device thread.
    bool finished_ = false;
    std::shared_ptr<AddressSlotMapping> address_slot_mapping_;
};

#endif // MSD_ARM_ATOM_H_
