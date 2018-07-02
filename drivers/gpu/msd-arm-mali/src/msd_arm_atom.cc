// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_atom.h"

#include "magma_util/macros.h"
#include "platform_trace.h"

MsdArmAtom::MsdArmAtom(std::weak_ptr<MsdArmConnection> connection, uint64_t gpu_address,
                       uint32_t slot, uint8_t atom_number, magma_arm_mali_user_data user_data,
                       int8_t priority)
    : trace_nonce_(TRACE_NONCE()), connection_(connection), gpu_address_(gpu_address), slot_(slot),
      priority_(priority), atom_number_(atom_number), user_data_(user_data)
{
}

void MsdArmAtom::set_dependencies(const DependencyList& dependencies)
{
    DASSERT(dependencies_.empty());
    dependencies_ = dependencies;
}

void MsdArmAtom::UpdateDependencies(bool* all_finished_out)
{
    for (auto& dependency : dependencies_) {
        if (dependency.atom) {
            if (dependency.atom->result_code() != kArmMaliResultRunning) {
                dependency.saved_result = dependency.atom->result_code();
                // Clear out the shared_ptr to ensure we won't get
                // arbitrarily-long dependency chains.
                dependency.atom = nullptr;
            }
        }
        // Technically a failure of a data dep could count as finishing (because
        // the atom will immediately fail), but for simplicity continue to wait
        // for all deps.
        if (dependency.atom) {
            *all_finished_out = false;
            return;
        }
    }
    *all_finished_out = true;
}

ArmMaliResultCode MsdArmAtom::GetFinalDependencyResult() const
{
    for (auto dependency : dependencies_) {
        // Should only be called after all dependencies are finished.
        DASSERT(!dependency.atom);
        if (dependency.saved_result != kArmMaliResultSuccess &&
            dependency.type != kArmMaliDependencyOrder)
            return dependency.saved_result;
    }
    return kArmMaliResultSuccess;
}

void MsdArmAtom::set_address_slot_mapping(std::shared_ptr<AddressSlotMapping> address_slot_mapping)
{
    if (address_slot_mapping) {
        DASSERT(!address_slot_mapping_);
        DASSERT(address_slot_mapping->connection());
        DASSERT(connection_.lock() == address_slot_mapping->connection());
    }
    address_slot_mapping_ = address_slot_mapping;
}

void MsdArmAtom::SetExecutionStarted() { execution_start_time_ = std::chrono::steady_clock::now(); }
