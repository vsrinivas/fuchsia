// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_REGISTERS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_REGISTERS_H_

#include <hwreg/bitfields.h>

namespace nvme {

// Register offsets defined in NVM Express base specification 2.0, section 3.1.3, "Controller
// Properties".
inline constexpr uint32_t NVME_REG_CAP = 0x00;
inline constexpr uint32_t NVME_REG_VS = 0x08;
inline constexpr uint32_t NVME_REG_INTMS = 0x0c;
inline constexpr uint32_t NVME_REG_INTMC = 0x0f;
inline constexpr uint32_t NVME_REG_CC = 0x14;
inline constexpr uint32_t NVME_REG_CSTS = 0x1c;
inline constexpr uint32_t NVME_REG_NSSR = 0x20;
inline constexpr uint32_t NVME_REG_AQA = 0x24;
inline constexpr uint32_t NVME_REG_ASQ = 0x28;
inline constexpr uint32_t NVME_REG_ACQ = 0x30;
inline constexpr uint32_t NVME_REG_CMBLOC = 0x38;
inline constexpr uint32_t NVME_REG_CMBSZ = 0x3c;
inline constexpr uint32_t NVME_REG_BPINFO = 0x40;
inline constexpr uint32_t NVME_REG_BPRSEL = 0x44;
inline constexpr uint32_t NVME_REG_BPMBL = 0x48;

inline constexpr uint32_t NVME_REG_DOORBELL_BASE = 0x1000;

// NVM Express base specification 2.0, section 3.1.3.1
class CapabilityReg : public hwreg::RegisterBase<CapabilityReg, uint64_t> {
 public:
  enum ControllerPowerScope {
    kNotReported = 0,
    kController = 1,
    kDomain = 2,
    kSubsystem = 3,
  };

  DEF_BIT(60, controller_ready_independent_media_supported);
  DEF_BIT(59, controller_ready_with_media_supported);
  DEF_BIT(58, subsystem_shutdown_supported);
  DEF_BIT(57, controller_memory_buffer_supported);
  DEF_BIT(56, persistent_memory_region_supported);
  DEF_FIELD(55, 52, memory_page_size_max);
  DEF_FIELD(51, 48, memory_page_size_min);
  DEF_ENUM_FIELD(ControllerPowerScope, 47, 46, controller_power_scope);
  DEF_BIT(45, boot_partition_support);
  DEF_BIT(44, no_io_command_set_support);
  DEF_BIT(43, identify_io_command_set_support);
  // Bits 42..38 are bits 5..1 of CAP.CSS in the NVME 2.0 spec.
  DEF_RSVDZ_FIELD(42, 38);
  DEF_BIT(37, nvm_command_set_support);
  DEF_BIT(36, nvm_subsystem_reset_supported);
  DEF_FIELD(35, 32, doorbell_stride);
  // Timeout is in 500ms units.
  DEF_FIELD(31, 24, timeout);
  // Bits 23..19 are reserved.
  DEF_BIT(18, vendor_specific_arbitration_supported);
  DEF_BIT(17, weighted_round_robin_arbitration_supported);
  DEF_BIT(16, contiguous_queues_required);
  DEF_FIELD(15, 0, max_queue_entries_raw);

  uint32_t memory_page_size_max_bytes() const { return 1 << (12 + memory_page_size_max()); }
  uint32_t memory_page_size_min_bytes() const { return 1 << (12 + memory_page_size_min()); }
  uint32_t doorbell_stride_bytes() const { return 1 << (2 + doorbell_stride()); }
  uint32_t timeout_ms() const { return static_cast<uint32_t>(timeout()) * 500; }
  uint32_t max_queue_entries() const { return static_cast<uint32_t>(max_queue_entries_raw()) + 1; }

  static auto Get() { return hwreg::RegisterAddr<CapabilityReg>(NVME_REG_CAP); }
};

// NVM Express base specification 2.0, section 3.1.3.2
class VersionReg : public hwreg::RegisterBase<VersionReg, uint32_t> {
 public:
  DEF_FIELD(31, 16, major);
  DEF_FIELD(15, 8, minor);
  DEF_FIELD(7, 0, tertiary);

  static auto Get() { return hwreg::RegisterAddr<VersionReg>(NVME_REG_VS); }
  static VersionReg FromVer(uint16_t major, uint8_t minor, uint8_t tertiary = 0) {
    return VersionReg().set_major(major).set_minor(minor).set_tertiary(tertiary);
  }

  friend bool operator<(const VersionReg& lhs, const VersionReg& rhs) {
    return lhs.reg_value() < rhs.reg_value();
  }
  friend bool operator>(const VersionReg& lhs, const VersionReg& rhs) { return rhs < lhs; }
  friend bool operator<=(const VersionReg& lhs, const VersionReg& rhs) { return !(lhs > rhs); }
  friend bool operator>=(const VersionReg& lhs, const VersionReg& rhs) { return !(lhs < rhs); }
};

// NVM Express base specification 2.0, section 3.1.3.{3,4}
class InterruptReg : public hwreg::RegisterBase<InterruptReg, uint32_t> {
 public:
  DEF_FIELD(31, 0, interrupts);

  static auto MaskSet() { return hwreg::RegisterAddr<InterruptReg>(NVME_REG_INTMS); }
  static auto MaskClear() { return hwreg::RegisterAddr<InterruptReg>(NVME_REG_INTMC); }
};

// NVM Express base specification 2.0, section 3.1.3.5
class ControllerConfigReg : public hwreg::RegisterBase<ControllerConfigReg, uint32_t> {
 public:
  enum ShutdownNotification {
    kNone = 0,
    kNormal = 1,
    kAbrupt = 2,
    kReserved = 3,
  };
  enum ArbitrationMechanism {
    kRoundRobin = 0,
    kWeightedRoundRobin = 1,
    kVendorSpecific = 7,
  };
  enum CommandSet {
    kNvm = 0,
    kAllIo = 6,
    kAdminOnly = 7,
  };
  DEF_BIT(24, controller_ready_independent_of_media);
  DEF_FIELD(23, 20, io_completion_queue_entry_size);
  DEF_FIELD(19, 16, io_submission_queue_entry_size);
  DEF_ENUM_FIELD(ShutdownNotification, 15, 14, shutdown_notification);
  DEF_ENUM_FIELD(ArbitrationMechanism, 13, 11, arbitration_mechanism);
  DEF_FIELD(10, 7, memory_page_size);
  DEF_ENUM_FIELD(CommandSet, 6, 4, io_command_set);
  DEF_BIT(0, enabled);

  static auto Get() { return hwreg::RegisterAddr<ControllerConfigReg>(NVME_REG_CC); }
};

// NVM Express base specification 2.0, section 3.1.3.6
class ControllerStatusReg : public hwreg::RegisterBase<ControllerStatusReg, uint32_t> {
 public:
  enum ShutdownStatus {
    kNoShutdown = 0,
    kOccurring = 1,
    kComplete = 2,
  };
  DEF_BIT(6, shutdown_type);
  DEF_BIT(5, processing_paused);
  DEF_BIT(4, subsystem_reset_occured);
  DEF_ENUM_FIELD(ShutdownStatus, 3, 2, shutdown_status);
  DEF_BIT(1, controller_fatal_status);
  DEF_BIT(0, ready);

  static auto Get() { return hwreg::RegisterAddr<ControllerStatusReg>(NVME_REG_CSTS); }
};

// NVM Express base specification 2.0, section 3.1.3.8
class AdminQueueAttributesReg : public hwreg::RegisterBase<AdminQueueAttributesReg, uint32_t> {
 public:
  DEF_FIELD(27, 16, completion_queue_size);
  DEF_FIELD(11, 0, submission_queue_size);
  static auto Get() { return hwreg::RegisterAddr<AdminQueueAttributesReg>(NVME_REG_AQA); }
};

// NVM Express base specification 2.0, section 3.1.3.{9,10}
class AdminQueueAddressReg : public hwreg::RegisterBase<AdminQueueAddressReg, uint64_t> {
 public:
  DEF_UNSHIFTED_FIELD(63, 12, addr);
  static auto SubmissionQueue() { return hwreg::RegisterAddr<AdminQueueAddressReg>(NVME_REG_ASQ); }
  static auto CompletionQueue() { return hwreg::RegisterAddr<AdminQueueAddressReg>(NVME_REG_ACQ); }
};

// NVM Express PCIe transport specification 1.0b section 3.1.2
class DoorbellReg : public hwreg::RegisterBase<DoorbellReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, value);

  static auto SubmissionQueue(size_t num, CapabilityReg& caps) {
    return hwreg::RegisterAddr<DoorbellReg>(
        NVME_REG_DOORBELL_BASE +
        ((2 * static_cast<uint32_t>(num)) * (4 << caps.doorbell_stride())));
  }

  static auto CompletionQueue(size_t num, CapabilityReg& caps) {
    return hwreg::RegisterAddr<DoorbellReg>(
        NVME_REG_DOORBELL_BASE +
        ((2 * static_cast<uint32_t>(num) + 1) * (4 << caps.doorbell_stride())));
  }
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_REGISTERS_H_
