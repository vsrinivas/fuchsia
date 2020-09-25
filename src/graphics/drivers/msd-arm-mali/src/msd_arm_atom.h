// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MSD_ARM_ATOM_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MSD_ARM_ATOM_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "address_space.h"
#include "magma_arm_mali_types.h"
#include "platform_semaphore.h"

class MsdArmAtom {
 public:
  struct Dependency {
    ArmMaliDependencyType type;
    std::shared_ptr<MsdArmAtom> atom;
    ArmMaliResultCode saved_result = kArmMaliResultRunning;
  };
  using DependencyList = std::vector<Dependency>;

  static constexpr uint64_t kInvalidGpuAddress = ~0ul;

  virtual ~MsdArmAtom() {}
  MsdArmAtom(std::weak_ptr<MsdArmConnection> connection, uint64_t gpu_address, uint32_t slot,
             uint8_t atom_number, magma_arm_mali_user_data user_data, int8_t priority,
             AtomFlags flags = static_cast<AtomFlags>(0));

  uint64_t trace_nonce() const { return trace_nonce_; }
  std::weak_ptr<MsdArmConnection> connection() const { return connection_; }
  uint64_t gpu_address() const { return gpu_address_; }
  void set_gpu_address(uint64_t gpu_address) { gpu_address_ = gpu_address; }
  uint32_t slot() const { return slot_; }
  uint8_t atom_number() const { return atom_number_; }
  const magma_arm_mali_user_data& user_data() const { return user_data_; }

  void set_require_cycle_counter() { require_cycle_counter_ = true; }
  void set_using_cycle_counter(bool using_cycle_counter) {
    using_cycle_counter_ = using_cycle_counter;
  }
  bool require_cycle_counter() const { return require_cycle_counter_; }
  bool using_cycle_counter() const { return using_cycle_counter_; }

  int8_t priority() const { return priority_; }
  AtomFlags flags() const { return flags_; }
  bool is_protected() const { return flags_ & kAtomFlagProtected; }
  bool IsDependencyOnly() const { return !gpu_address_; }

  void set_dependencies(const DependencyList& dependencies);
  void UpdateDependencies(bool* all_finished_out);

  // Returns a failure result code if a data dependency of this atom failed.
  ArmMaliResultCode GetFinalDependencyResult() const;

  ArmMaliResultCode result_code() const { return result_code_; }

  // These methods should only be called on the device thread.
  void set_result_code(ArmMaliResultCode code) {
    DASSERT(result_code_ == kArmMaliResultRunning);

    result_code_ = code;
  }
  bool hard_stopped() const { return hard_stopped_; }
  void set_hard_stopped() { hard_stopped_ = true; }
  bool soft_stopped() const { return soft_stopped_; }
  void set_soft_stopped(bool stopped) {
    soft_stopped_ = stopped;
    soft_stopped_time_ = stopped ? magma::get_monotonic_ns() : 0;
  }

  // Preempted by a timer interrupt (not by a higher priority atom)
  bool preempted() const { return preempted_; }
  void set_preempted(bool preempted) { preempted_ = preempted; }

  void set_execution_start_time(std::chrono::time_point<std::chrono::steady_clock> time) {
    execution_start_time_ = time;
  }
  void set_tick_start_time(std::chrono::time_point<std::chrono::steady_clock> time) {
    tick_start_time_ = time;
  }
  std::chrono::time_point<std::chrono::steady_clock> execution_start_time() const {
    return execution_start_time_;
  }
  std::chrono::time_point<std::chrono::steady_clock> tick_start_time() const {
    return tick_start_time_;
  }

  // These methods should only be called on the device thread.
  void set_address_slot_mapping(std::shared_ptr<AddressSlotMapping> address_slot_mapping);
  std::shared_ptr<AddressSlotMapping> address_slot_mapping() const { return address_slot_mapping_; }

  virtual bool is_soft_atom() const { return false; }

  // Use different names for different slots so they'll line up cleanly in the
  // trace viewer.
  static const char* AtomRunningString(uint32_t slot) {
    switch (slot) {
      case 0:
        return "Atom Slot 0";
      case 1:
        return "Atom Slot 1";
      case 2:
        return "Atom Slot 2";
      default:
        DASSERT(false);
        return "Unknown Atom Slot";
    }
  }

  // TODO: Remove this when trace generated JSON can support 64bit ints
  // without this hack. (fxbug.dev/22971)
  uint64_t slot_id() { return slot_ * 2000; }

  virtual std::vector<std::string> DumpInformation();

 private:
  // The following data is immmutable after construction.
  const uint64_t trace_nonce_;
  const std::weak_ptr<MsdArmConnection> connection_;
  uint64_t gpu_address_;
  const uint32_t slot_;
  const int8_t priority_;
  const AtomFlags flags_{};
  bool require_cycle_counter_ = false;
  DependencyList dependencies_;
  // Assigned by client.
  const uint8_t atom_number_;
  const magma_arm_mali_user_data user_data_;

  // This data is mutable after construction from the device thread.
  ArmMaliResultCode result_code_ = kArmMaliResultRunning;
  std::shared_ptr<AddressSlotMapping> address_slot_mapping_;
  std::chrono::time_point<std::chrono::steady_clock> execution_start_time_;
  std::chrono::time_point<std::chrono::steady_clock> tick_start_time_;
  bool hard_stopped_ = false;
  bool soft_stopped_ = false;
  uint64_t soft_stopped_time_ = {};
  bool using_cycle_counter_ = false;
  bool preempted_ = false;
};

// Soft atoms don't actually execute in hardware.
class MsdArmSoftAtom : public MsdArmAtom {
 public:
  static std::shared_ptr<MsdArmSoftAtom> cast(std::shared_ptr<MsdArmAtom> atom) {
    if (atom->is_soft_atom())
      return std::static_pointer_cast<MsdArmSoftAtom>(atom);
    return nullptr;
  }

  MsdArmSoftAtom(std::weak_ptr<MsdArmConnection> connection, AtomFlags soft_flags,
                 std::shared_ptr<magma::PlatformSemaphore> platform_semaphore, uint8_t atom_number,
                 magma_arm_mali_user_data user_data)
      : MsdArmAtom(connection, kInvalidGpuAddress, 0, atom_number, user_data, 0, soft_flags),
        platform_semaphore_(platform_semaphore) {}

  AtomFlags soft_flags() const { return flags(); }
  std::shared_ptr<magma::PlatformSemaphore> platform_semaphore() const {
    return platform_semaphore_;
  }

  bool is_soft_atom() const override { return true; }
  virtual std::vector<std::string> DumpInformation() override;

 private:
  // Immutable after construction.
  const std::shared_ptr<magma::PlatformSemaphore> platform_semaphore_;
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MSD_ARM_ATOM_H_
