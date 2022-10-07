// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_POWER_CONTROLLER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_POWER_CONTROLLER_H_

#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/status.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

// Command sent to the PCU (power controller)'s firmware.
struct PowerControllerCommand {
  uint8_t command;
  uint8_t param1 = 0;
  uint8_t param2 = 0;
  uint64_t data;

  // The amount of time to wait for the PCU firmware to complete the command.
  //
  // This time is measured from the moment the command is submitted to the PCU
  // firmware via the GT Driver Mailbox. Consequently,
  // PowerController::Transact() execution may take longer than this timeout.
  // See the method-level comments for details.
  //
  // If this is zero, the GT Driver Mailbox state will not be consulted at all
  // after the command is posted.
  int timeout_us;
};

// Communicates with the firmware on the PCU (power controller).
//
// The PCU firmware is also called PCODE (power microcontroller microcode) in
// Intel's documentation. The avenue for communication is called the GT Driver
// Mailbox (sometimes abbreviated to "GT Mailbox") in Intel's documentation.
//
// All higher-level commands are built on top of Transact(). See the Transact()
// comments for low-level details on communicating with the PCU firmware.
class PowerController {
 public:
  // Behavior when the PCU-reported state doesn't match the requested state.
  enum class RetryBehavior {
    // Issue the state change request once. The caller will recover from
    // ZX_ERR_IO_REFUSED errors, which indicate that the current state doesn't
    // match the requested state.
    kNoRetry = 0,

    // Repeat the state change request until the PCU firmware reports that the
    // current state matches the request. Give up when it becomes highly likely
    // that an external factor is preventing the PCU's current state from
    // matching the requested state. The caller cannot recover from
    // ZX_ERR_IO_REFUSED errors.
    kRetryUntilStateChanges = 1,
  };

  explicit PowerController(fdf::MmioBuffer* mmio_buffer);

  PowerController(const PowerController&) = delete;
  PowerController(PowerController&&) = delete;
  PowerController& operator=(const PowerController&) = delete;
  PowerController& operator=(PowerController&&) = delete;

  // Trivially destructible.
  ~PowerController() = default;

  // Performs a command-response exchange with the PCU firmware.
  //
  // Returns ZX_ERR_IO_MISSED_DEADLINE if a timeout occurs while waiting for the
  // PCU firmware. This usually happens if the PCU does not complete `command`
  // in time, but can also indicate that the PCU firmware was already performing
  // on a different command, and did not become available in a reasonable amount
  // of time.
  //
  // In case of success, returns the 64-bit value in the GT Mailbox Data
  // Low/High registers.
  //
  // Before submitting `command` to the PCU firmware via the GT Mailbox
  // registers, this method waits (for quite a while) for any ongoing command to
  // finish executing. We adopted this strategy because successful execution of
  // PCU commands is usually critical to the driver's operation, so we trade off
  // some waiting time in return for maximizing the odds of successful
  // execution. The consequence of this approach is that Transact() may take
  // more than `command.timeout_us` to complete.
  zx::status<uint64_t> Transact(PowerControllerCommand command);

  // Informs the PCU of the display engine's voltage requirements.
  //
  // Returns ZX_ERR_IO_MISSED_DEADLINE if a timeout occurs while communicating
  // with the the PCU firmware. This indicates a problem in the PCU firmware. We
  // should not make any clocking changes if this happens.
  //
  // Returns ZX_ERR_IO_REFUSED if the PCU firmware did not set the voltage to
  // the requested level. This is an acceptable outcome when `voltage_level` is
  // not the maximum level. For example, another consumer (device that shares
  // the voltage rail with the display engine) may have requested a higher
  // voltage level.
  //
  // `voltage_level` must be a valid display engine voltage level. All known
  // display engines use levels 0-3.
  zx::status<> RequestDisplayVoltageLevel(int voltage_level, RetryBehavior retry_behavior);

  // Sets the display engine's block TCCOLD (Type C Cold power state) flag.
  //
  // Returns ZX_ERR_IO_MISSED_DEADLINE if a timeout occurs while communicating
  // with the the PCU firmware. This indicates a problem in the PCU firmware. We
  // should stop using Type C ports if this happens.
  //
  // Returns ZX_ERR_IO_REFUSED if the PCU firmware did not bring the Type C
  // subsystem into the state implied by the blocking request. This is an
  // acceptable outcome when `blocked` is false. For example, the Type C ports
  // may be used by another client.
  //
  // The Type C system must be brought out of the cold power state before
  // accessing any registers in the FIA (Flexible IO Adapter) or in the Type C
  // PHYs. The cold power state must remain blocked as long as the display
  // engine uses any main link or AUX channel in a Type C connector.
  //
  // This method implements the communication protocol for Tiger Lake's PCU
  // firmware. Other processors use different protocols.
  zx::status<> SetDisplayTypeCColdBlockingTigerLake(bool blocked, RetryBehavior retry_behavior);

 private:
  fdf::MmioBuffer* mmio_buffer_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_POWER_CONTROLLER_H_
