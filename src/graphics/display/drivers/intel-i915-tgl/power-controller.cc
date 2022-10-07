// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/power-controller.h"

#include <lib/ddk/debug.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/clock.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-gt-mailbox.h"

namespace i915_tgl {

namespace {

// The amount of microseconds to wait for PCU to complete a previous command.
//
// This should be at least as large as all known command timeouts below.
constexpr int kPreviousCommandTimeoutUs = 200;

// Timeout for the PCU firmware to reply to a voltage change requirement.
constexpr int kVoltageLevelRequestReplyTimeoutUs = 150;

// Timeout for the PCU firmware to execute a voltage change requirement.
constexpr int kVoltageLevelRequestTotalTimeoutUs = 3'000;  // 3ms

// Timeout for the PCU firmware to reply a TCCOLD blocking change request.
constexpr int kTypeCColdBlockingChangeReplyTimeoutUs = 200;

// Timeout for the PCU firmware to execute a voltage change requirement.
constexpr int kTypeCColdBlockingChangeTotalTimeoutUs = 600;

}  // namespace

PowerController::PowerController(fdf::MmioBuffer* mmio_buffer) : mmio_buffer_(mmio_buffer) {
  ZX_DEBUG_ASSERT(mmio_buffer);
}

zx::status<uint64_t> PowerController::Transact(PowerControllerCommand command) {
  auto mailbox_interface = tgl_registers::PowerMailboxInterface::Get().FromValue(0);

  if (!PollUntil([&] { return !mailbox_interface.ReadFrom(mmio_buffer_).has_active_transaction(); },
                 zx::usec(1), kPreviousCommandTimeoutUs)) {
    zxlogf(WARNING, "Timed out while waiting for PCU to finish pre-existing work");
    return zx::error_status(ZX_ERR_IO_MISSED_DEADLINE);
  }

  auto mailbox_data0 = tgl_registers::PowerMailboxData0::Get().FromValue(0);
  mailbox_data0.set_reg_value(static_cast<uint32_t>(command.data)).WriteTo(mmio_buffer_);
  auto mailbox_data1 = tgl_registers::PowerMailboxData1::Get().FromValue(0);
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
    return zx::error_status(ZX_ERR_IO_MISSED_DEADLINE);
  }

  const uint32_t data_low = mailbox_data0.ReadFrom(mmio_buffer_).reg_value();
  const uint32_t data_high = mailbox_data1.ReadFrom(mmio_buffer_).reg_value();
  const uint64_t data = (uint64_t{data_high} << 32) | data_low;
  return zx::ok(data);
}

zx::status<> PowerController::RequestDisplayVoltageLevel(int voltage_level,
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
    zx::status<uint64_t> mailbox_result = Transact({
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

  return zx::error_status(ZX_ERR_IO_REFUSED);
}

zx::status<> PowerController::SetDisplayTypeCColdBlockingTigerLake(bool blocked,
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
    zx::status<uint64_t> mailbox_result = Transact({
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

  return zx::error_status(ZX_ERR_IO_REFUSED);
}

}  // namespace i915_tgl
