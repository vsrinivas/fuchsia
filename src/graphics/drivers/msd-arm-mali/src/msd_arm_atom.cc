// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_atom.h"

#include <fbl/string_printf.h>

#include "magma_util/macros.h"
#include "msd_arm_connection.h"
#include "platform_trace.h"

MsdArmAtom::MsdArmAtom(std::weak_ptr<MsdArmConnection> connection, uint64_t gpu_address,
                       uint32_t slot, uint8_t atom_number, magma_arm_mali_user_data user_data,
                       int8_t priority, AtomFlags flags)
    : trace_nonce_(TRACE_NONCE()),
      connection_(connection),
      gpu_address_(gpu_address),
      slot_(slot),
      priority_(priority),
      flags_(flags),
      atom_number_(atom_number),
      user_data_(user_data) {}

void MsdArmAtom::set_dependencies(const DependencyList& dependencies) {
  DASSERT(dependencies_.empty());
  dependencies_ = dependencies;
}

void MsdArmAtom::UpdateDependencies(bool* all_finished_out) {
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

ArmMaliResultCode MsdArmAtom::GetFinalDependencyResult() const {
  for (auto dependency : dependencies_) {
    // Should only be called after all dependencies are finished.
    DASSERT(!dependency.atom);
    if (dependency.saved_result != kArmMaliResultSuccess &&
        dependency.type != kArmMaliDependencyOrder)
      return dependency.saved_result;
  }
  return kArmMaliResultSuccess;
}

void MsdArmAtom::set_address_slot_mapping(
    std::shared_ptr<AddressSlotMapping> address_slot_mapping) {
  if (address_slot_mapping) {
    DASSERT(!address_slot_mapping_);
    DASSERT(address_slot_mapping->connection());
    DASSERT(connection_.lock() == address_slot_mapping->connection());
  }
  address_slot_mapping_ = address_slot_mapping;
}

std::vector<std::string> MsdArmAtom::DumpInformation() {
  auto locked_connection = connection_.lock();
  uint64_t client_id = locked_connection ? locked_connection->client_id() : 0;
  uint32_t address_slot = address_slot_mapping_ ? address_slot_mapping_->slot_number() : UINT32_MAX;
  std::vector<std::string> result;
  result.push_back(fbl::StringPrintf("Atom gpu_va 0x%lx number %d slot %d client_id %ld flags 0x%x "
                                     "priority %d hard_stop %d soft_stop %d, address slot %d",
                                     gpu_address_, atom_number_, slot_, client_id, flags_,
                                     priority_, hard_stopped_, soft_stopped_, address_slot)
                       .c_str());
  for (auto dependency : dependencies_) {
    if (dependency.atom) {
      result.push_back(fbl::StringPrintf("  Dependency on atom number %d type %d (result %d)",
                                         dependency.atom->atom_number(), dependency.type,
                                         dependency.atom->result_code())
                           .c_str());
    } else {
      result.push_back(fbl::StringPrintf("  Dependency on saved result 0x%x type %d",
                                         dependency.saved_result, dependency.type)
                           .c_str());
    }
  }

  return result;
}

std::vector<std::string> MsdArmSoftAtom::DumpInformation() {
  std::vector<std::string> result = MsdArmAtom::DumpInformation();

  result.push_back(fbl::StringPrintf("  Semaphore koid %ld", platform_semaphore_->id()).c_str());
  return result;
}
