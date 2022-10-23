// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/power-controller.h"

#include <lib/ddk/debug.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/clock.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915/poll-until.h"
#include "src/graphics/display/drivers/intel-i915/registers-gt-mailbox.h"

namespace i915 {

namespace {

// The amount of microseconds to wait for PCU to complete a previous command.
//
// This should be at least as large as all known command timeouts below.
constexpr int kPreviousCommandTimeoutUs = 200;

// Timeout for the PCU firmware to reply to a voltage change request.
constexpr int kVoltageLevelRequestReplyTimeoutUs = 150;

// Timeout for the PCU firmware to execute a voltage change request.
constexpr int kVoltageLevelRequestTotalTimeoutUs = 3'000;  // 3ms

// Timeout for the PCU firmware to reply to a TCCOLD blocking change request.
constexpr int kTypeCColdBlockingChangeReplyTimeoutUs = 200;

// Timeout for the PCU firmware to execute a TCCOLD blocking change request.
constexpr int kTypeCColdBlockingChangeTotalTimeoutUs = 600;

// Timeout for the PCU firmware to reply to a SAGV enablement change request.
constexpr int kSystemAgentEnablementChangeReplyTimeoutUs = 150;

// Timeout for the PCU firmware to execute a SAGV enablement change request.
constexpr int kSystemAgentEnablementChangeTotalTimeoutUs = 1'000;  // 1ms

// Timeout for the PCU firmware to reply to a memory subsystem info request.
constexpr int kGetMemorySubsystemInfoReplyTimeoutUs = 150;

// Timeout for the PCU firmware to reply to a memory latency info request.
constexpr int kGetMemoryLatencyReplyTimeoutUs = 100;

}  // namespace

PowerController::PowerController(fdf::MmioBuffer* mmio_buffer) : mmio_buffer_(mmio_buffer) {
  ZX_DEBUG_ASSERT(mmio_buffer);
}

zx::result<uint64_t> PowerController::Transact(PowerControllerCommand command) {
  auto mailbox_interface = registers::PowerMailboxInterface::Get().FromValue(0);

  if (!PollUntil([&] { return !mailbox_interface.ReadFrom(mmio_buffer_).has_active_transaction(); },
                 zx::usec(1), kPreviousCommandTimeoutUs)) {
    zxlogf(WARNING, "Timed out while waiting for PCU to finish pre-existing work");
    return zx::error_result(ZX_ERR_IO_MISSED_DEADLINE);
  }

  auto mailbox_data0 = registers::PowerMailboxData0::Get().FromValue(0);
  mailbox_data0.set_reg_value(static_cast<uint32_t>(command.data)).WriteTo(mmio_buffer_);
  auto mailbox_data1 = registers::PowerMailboxData1::Get().FromValue(0);
  mailbox_data1.set_reg_value(static_cast<uint32_t>(command.data >> 32)).WriteTo(mmio_buffer_);
  mailbox_interface.set_command_code(command.command)
      .set_param1(command.param1)
      .set_param2(command.param2)
      .set_has_active_transaction(true)
      .WriteTo(mmio_buffer_);

  if (command.timeout_us == 0) {
    return zx::ok(0);
  }

  if (!PollUntil([&] { return !mailbox_interface.ReadFrom(mmio_buffer_).has_active_transaction(); },
                 zx::usec(1), command.timeout_us)) {
    return zx::error_result(ZX_ERR_IO_MISSED_DEADLINE);
  }

  const uint32_t data_low = mailbox_data0.ReadFrom(mmio_buffer_).reg_value();
  const uint32_t data_high = mailbox_data1.ReadFrom(mmio_buffer_).reg_value();
  const uint64_t data = (uint64_t{data_high} << 32) | data_low;
  return zx::ok(data);
}

zx::result<> PowerController::RequestDisplayVoltageLevel(int voltage_level,
                                                         RetryBehavior retry_behavior) {
  // This operation is documented in the Clocking sections in Intel's display
  // engine PRMs.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 section "Display Voltage
  //             Frequency Switching" > "Sequence Before Frequency Change" and
  //             "Sequence After Frequency Change", page 195
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock
  //            Frequency", pages 138-139
  // Skylake: IHD-OS-SKL-Vol 12-05.16 "Skylake Sequences for Changing CD Clock
  //          Frequency", pages 135-136

  // ZX_DEBUG_ASSERT() is appropriate for most cases where individual parameters
  // are set incorrectly, but only correct MMIO addresses are accessed. However,
  // confusing the PCU firmware can have pretty catastrophic consequences for
  // the system, so we're very strict here.
  ZX_ASSERT(voltage_level >= 0);
  ZX_ASSERT(voltage_level <= 3);

  const zx::time deadline = (retry_behavior == RetryBehavior::kRetryUntilStateChanges)
                                ? zx::deadline_after(zx::usec(kVoltageLevelRequestTotalTimeoutUs))
                                : zx::time::infinite_past();

  do {
    zx::result<uint64_t> mailbox_result = Transact({
        .command = 0x07,
        .data = static_cast<uint64_t>(voltage_level),
        .timeout_us = kVoltageLevelRequestReplyTimeoutUs,
    });
    if (mailbox_result.is_error()) {
      return mailbox_result.take_error();
    }
    const bool success = (mailbox_result.value() & 1) == 1;
    if (success) {
      return zx::ok();
    }
  } while (zx::clock::get_monotonic() < deadline);

  return zx::error_result(ZX_ERR_IO_REFUSED);
}

zx::result<> PowerController::SetDisplayTypeCColdBlockingTigerLake(bool blocked,
                                                                   RetryBehavior retry_behavior) {
  // This operation is documented in IHD-OS-TGL-Vol 12-1.22-Rev2.0, sections
  // "GT Driver Mailbox to Block TCCOLD" and "GT Driver Mailbox to Unblock
  // TCCOLD" sections in Intel's display engine PRMs.
  //
  // IHD-OS-LKF-Vol 12-4.21 also documents the TCCOLD concept, but Lakefield's
  // PCU firmware uses a different API for managing TCCOLD.

  const zx::time deadline =
      (retry_behavior == RetryBehavior::kRetryUntilStateChanges)
          ? zx::deadline_after(zx::usec(kTypeCColdBlockingChangeTotalTimeoutUs))
          : zx::time::infinite_past();

  const uint64_t command_data = blocked ? 0 : 1;
  do {
    zx::result<uint64_t> mailbox_result = Transact({
        .command = 0x26,
        .data = command_data,
        .timeout_us = kTypeCColdBlockingChangeReplyTimeoutUs,
    });
    if (mailbox_result.is_error()) {
      return mailbox_result.take_error();
    }
    const bool type_c_controller_in_cold_state = (mailbox_result.value() & 1) == 1;
    if (type_c_controller_in_cold_state != blocked) {
      return zx::ok();
    }
  } while (zx::clock::get_monotonic() < deadline);

  return zx::error_result(ZX_ERR_IO_REFUSED);
}

zx::result<> PowerController::SetSystemAgentGeyservilleEnabled(bool enabled,
                                                               RetryBehavior retry_behavior) {
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 section
  //             "MAILBOX_GTDRIVER_CMD_DE_LTR_SETTING", pages 214-215
  // DG1: IHD-OS-DG1-Vol 12-2.21 section ""MAILBOX_GTDRIVER_CMD_DE_LTR_SETTING",
  //      pages 171-172
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "System Agent Geyserville (SAGV)", page
  //            206
  // Skylake: IHD-OS-SKL-Vol 12-05.16 "System Agent Geyserville (SAGV)", pages
  //          197-198

  const zx::time deadline =
      (retry_behavior == RetryBehavior::kRetryUntilStateChanges)
          ? zx::deadline_after(zx::usec(kSystemAgentEnablementChangeTotalTimeoutUs))
          : zx::time::infinite_past();

  // The data is documented as the EL_THLD (Threshold) LTR (most likely "Latency
  // Tolerance Reporting") override on Tiger Lake and DG1.
  const uint64_t command_data = enabled ? 3 : 0;
  do {
    zx::result<uint64_t> mailbox_result = Transact({
        .command = 0x21,
        .data = command_data,
        .timeout_us = kSystemAgentEnablementChangeReplyTimeoutUs,
    });
    if (mailbox_result.is_error()) {
      return mailbox_result.take_error();
    }
    const bool success = (mailbox_result.value() & 1) == 1;
    if (success) {
      return zx::ok();
    }
  } while (zx::clock::get_monotonic() < deadline);

  return zx::error_result(ZX_ERR_IO_REFUSED);
}

zx::result<uint32_t> PowerController::GetSystemAgentBlockTimeUsTigerLake() {
  // Documented in the "Display Watermark Programming" > "SAGV Block Time"
  // section in the PRMs.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 436-437
  // DG1: IHD-OS-DG1-Vol 12-2.21 page 362

  zx::result<uint64_t> mailbox_result = Transact({
      .command = 0x23,
      .param1 = 0,
      .param2 = 0,
      .data = 0,
      .timeout_us = kGetMemoryLatencyReplyTimeoutUs,
  });
  if (mailbox_result.is_error()) {
    return mailbox_result.take_error();
  }

  // This PCU command returns an error code in the Command/Error Code field
  // of the Mailbox Interface register.
  auto mailbox_interface = registers::PowerMailboxInterface::Get().ReadFrom(mmio_buffer_);
  if (mailbox_interface.command_code() != 0) {
    return zx::error_result(ZX_ERR_IO_REFUSED);
  }

  return zx::ok(static_cast<uint32_t>(mailbox_result.value()));
}

zx::result<uint32_t> PowerController::GetSystemAgentBlockTimeUsKabyLake() {
  // Documented in the "Display Watermark Programming" > "SAGV Block Time"
  // section in the PRMs.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 209
  // Skylake: IHD-OS-SKL-Vol 12-05.16 page 200

  return zx::ok(30);
}

zx::result<std::array<uint8_t, 8>> PowerController::GetRawMemoryLatencyDataUs() {
  // Documented in the "Display Watermark Programming" > "Memory Values" section
  // in the PRMs.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 435-436
  // DG1: IHD-OS-DG1-Vol 12-2.21 pages 361-362
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 pages 208-209
  // Skylake: IHD-OS-SKL-Vol 12-05.16 pages 199-200

  uint32_t latency_data[2];

  for (int values_index = 0; values_index < 2; ++values_index) {
    zx::result<uint64_t> mailbox_result = Transact({
        .command = 0x06,
        .param1 = 0,
        .param2 = 0,
        .data = static_cast<uint64_t>(values_index),
        .timeout_us = kGetMemoryLatencyReplyTimeoutUs,
    });
    if (mailbox_result.is_error()) {
      return mailbox_result.take_error();
    }

    // This PCU command returns an error code in the Command/Error Code field
    // of the Mailbox Interface register.
    auto mailbox_interface = registers::PowerMailboxInterface::Get().ReadFrom(mmio_buffer_);
    if (mailbox_interface.command_code() != 0) {
      return zx::error_result(ZX_ERR_IO_REFUSED);
    }
    latency_data[values_index] = static_cast<uint32_t>(mailbox_result.value());
  }

  std::array<uint8_t, 8> latency_levels;
  static_assert(sizeof(latency_data) == sizeof(latency_levels));
  std::memcpy(latency_levels.data(), latency_data, sizeof(latency_data));
  return zx::ok(latency_levels);
}

namespace {

// MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_GLOBAL_INFO result.
//
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 212-213
// DG1: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 169-170
class MemorySubsystemGlobalConfig
    : public hwreg::RegisterBase<MemorySubsystemGlobalConfig, uint64_t> {
 public:
  DEF_FIELD(11, 8, enabled_qgv_point_count);
  DEF_FIELD(7, 4, populated_channel_count);
  DEF_ENUM_FIELD(MemorySubsystemInfo::RamType, 3, 0, ddr_type_select);

  static auto GetFromValue(uint64_t mailbox_data) {
    return hwreg::RegisterAddr<MemorySubsystemGlobalConfig>(0).FromValue(mailbox_data);
  }
};

// MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_QGV_POINT_INFO result.
//
//
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 212-213
// DG1: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 169-170
class MemorySubsystemPointInfo : public hwreg::RegisterBase<MemorySubsystemPointInfo, uint64_t> {
 public:
  // DRAM timings. See `MemorySubsystemInfo` for explanations.
  DEF_FIELD(48, 40, tras_dclks);
  DEF_FIELD(39, 32, trdpre_dclks);
  DEF_FIELD(31, 24, trcd_dclks);
  DEF_FIELD(23, 16, trp_dclks);

  // DRAM clock in multiples of 16.6666 MHz.
  DEF_FIELD(15, 0, dclk_multiplier);

  static auto GetFromValue(uint64_t mailbox_data) {
    return hwreg::RegisterAddr<MemorySubsystemPointInfo>(0).FromValue(mailbox_data);
  }
};

}  // namespace

// static
MemorySubsystemInfo::GlobalInfo MemorySubsystemInfo::GlobalInfo::CreateFromMailboxDataTigerLake(
    uint64_t mailbox_data) {
  auto global_config = MemorySubsystemGlobalConfig::GetFromValue(mailbox_data);
  return MemorySubsystemInfo::GlobalInfo{
      .ram_type = global_config.ddr_type_select(),
      .memory_channel_count = static_cast<int8_t>(global_config.populated_channel_count()),
      .agent_point_count = static_cast<int8_t>(global_config.enabled_qgv_point_count()),
  };
}

MemorySubsystemInfo::AgentPoint MemorySubsystemInfo::AgentPoint::CreateFromMailboxDataTigerLake(
    uint64_t mailbox_data) {
  auto point_info = MemorySubsystemPointInfo::GetFromValue(mailbox_data);
  return MemorySubsystemInfo::AgentPoint{
      // The cast is lossless because the underlying field is 16 bits. The
      // multiplication does not overflow because the maximum result is
      // 1,092,206,310 which fits in 31 bits.
      .dram_clock_khz =
          static_cast<int32_t>(static_cast<int32_t>(point_info.dclk_multiplier()) * 16'666),

      // The casts are lossless because the underlying fields are 9 bits.
      .row_precharge_to_open_cycles = static_cast<int16_t>(point_info.trp_dclks()),
      .row_access_to_column_access_delay_cycles = static_cast<int16_t>(point_info.trcd_dclks()),
      .read_to_precharge_cycles = static_cast<int16_t>(point_info.trdpre_dclks()),
      .row_activate_to_precharge_cycles = static_cast<int16_t>(point_info.tras_dclks()),
  };
}

zx::result<MemorySubsystemInfo> PowerController::GetMemorySubsystemInfoTigerLake() {
  // Documented in the "Mailbox Commands" > "MAILBOX_GTRDIVER_CMD_MEM_SS_INFO"
  // section of the PRMs.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 212-213
  // DG1: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 169-170

  MemorySubsystemInfo result;

  {
    // MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_GLOBAL_INFO in the PRM.
    zx::result<uint64_t> global_info = Transact({
        .command = 0x0d,
        .param1 = 0,
        .param2 = 0,
        .data = 0,
        .timeout_us = kGetMemorySubsystemInfoReplyTimeoutUs,
    });
    if (global_info.is_error()) {
      return global_info.take_error();
    }
    zxlogf(TRACE, "MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_GLOBAL_INFO - %lx",
           global_info.value());
    result.global_info =
        MemorySubsystemInfo::GlobalInfo::CreateFromMailboxDataTigerLake(global_info.value());
  }

  const int point_count = result.global_info.agent_point_count;
  for (int point_index = 0; point_index < point_count; ++point_index) {
    // MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_QGV_POINT_INFO in the
    // PRM.
    zx::result<uint64_t> point_info = Transact({
        .command = 0x0d,
        .param1 = 1,
        .param2 = static_cast<uint8_t>(point_index),
        .data = 0,
        .timeout_us = kGetMemorySubsystemInfoReplyTimeoutUs,
    });
    if (point_info.is_error()) {
      return point_info.take_error();
    }

    // This PCU command returns an error code in the Command/Error Code field
    // of the Mailbox Interface register.
    auto mailbox_interface = registers::PowerMailboxInterface::Get().ReadFrom(mmio_buffer_);
    if (mailbox_interface.command_code() != 0) {
      return zx::error_result(ZX_ERR_IO_REFUSED);
    }

    zxlogf(TRACE, "MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_QGV_POINT_INFO - %lx",
           point_info.value());
    result.points[point_index] =
        MemorySubsystemInfo::AgentPoint::CreateFromMailboxDataTigerLake(point_info.value());
  }

  return zx::ok(result);
}

}  // namespace i915
