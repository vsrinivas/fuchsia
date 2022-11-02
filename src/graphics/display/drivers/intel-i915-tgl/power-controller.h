// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_POWER_CONTROLLER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_POWER_CONTROLLER_H_

#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/result.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/scoped-value-change.h"

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

// Memory information reported by the PCU.
//
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 212-213
// DG1: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 169-170
struct MemorySubsystemInfo {
  // Documented values for the `ram_type` field.
  enum class RamType {
    kDoubleDataRam4 = 0,          // DDRAM 4
    kDoubleDataRam5 = 1,          // DDRAM 5
    kLowPowerDoubleDataRam5 = 2,  // LPDDRAM5
    kLowPowerDoubleDataRam4 = 3,  // LPDDRAM4
    kDoubleDataRam3 = 4,          // DDRAM 3
    kLowPowerDoubleDataRam3 = 5,  // LPDDRAM3
  };

  struct GlobalInfo {
    RamType ram_type;
    int memory_channel_count;  // Number of populated DDRAM channels.
    int agent_point_count;     // Number of enabled QGV points.

    // `mailbox_data` should be the mailbox data contents after a successful
    // MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_GLOBAL_INFO command.
    static GlobalInfo CreateFromMailboxDataTigerLake(uint64_t mailbox_data);
  };

  struct AgentPoint {
    // DRAM clock, in kHz.
    //
    // All inter-command latencies below are specified in terms of this clock.
    int32_t dram_clock_khz;

    // tRP: Latency from a precharge to the next row open.
    int16_t row_precharge_to_open_cycles;

    // tRCD: Latency from a row access to the next column access.
    int16_t row_access_to_column_access_delay_cycles;

    // tRDPRE / tRTP: Latency from a read to the next precharge.
    int16_t read_to_precharge_cycles;

    // tRAS: Latency from a row active to the next row precharge.
    int16_t row_activate_to_precharge_cycles;

    // `mailbox_data` should be the mailbox data contents after a successful
    // MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_QGV_POINT_INFO command.
    static AgentPoint CreateFromMailboxDataTigerLake(uint64_t mailbox_data);
  };

  GlobalInfo global_info;

  static constexpr int kMaxPointCount = 16;
  AgentPoint points[kMaxPointCount];
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
  zx::result<uint64_t> Transact(PowerControllerCommand command);

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
  zx::result<> RequestDisplayVoltageLevel(int voltage_level, RetryBehavior retry_behavior);

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
  zx::result<> SetDisplayTypeCColdBlockingTigerLake(bool blocked, RetryBehavior retry_behavior);

  // Sets the display engine's SAGV (System Agent Geyserville) enabled flag.
  //
  // Returns ZX_ERR_IO_MISSED_DEADLINE if a timeout occurs while communicating
  // with the the PCU firmware. This indicates a problem in the PCU firmware. We
  // should assume that the SAGV is stuck enabled and configure the display
  // engine's pipes and planes accordingly.
  //
  // Returns ZX_ERR_IO_REFUSED if the PCU firmware did not bring the system
  // agent subsystem into the state implied by the enablement request. This is
  // an acceptable outcome when `enabled` is true.
  //
  // This method implements the communication protocol for Kaby Lake and Skylake
  // PCUs. The protocol is supported by Tiger Lake PCUs, but has been superseded
  // by a more fine-grained version.
  zx::result<> SetSystemAgentGeyservilleEnabled(bool enabled, RetryBehavior retry_behavior);

  // Reads the SAGV (System Agent Geyserville) blocking time.
  //
  // Returns the SAGV Block Time, in microseconds.
  //
  // Returns ZX_ERR_IO_MISSED_DEADLINE if a timeout occurs while communicating
  // with the the PCU firmware. Returns ZX_ERR_IO_REFUSED if the PCU firmware
  // reports an error. In either case, the display engine's planes cannot be
  // used safely.
  //
  // This method implements the communication protocol for the Tiger Lake PCU.
  // The protocol is not supported on Kaby Lake and Skylake PCUs.
  zx::result<uint32_t> GetSystemAgentBlockTimeUsTigerLake();

  // Reads the SAGV (System Agent Geyserville) blocking time.
  //
  // Returns the SAGV Block Time, in microseconds.
  //
  // This method has the same signature as GetSystemAgentBlockTimeUsTigerLake()
  // for programming convenience. On Kaby Lake and Skylake PCUs, the SAGV
  // blocking time is constant.
  zx::result<uint32_t> GetSystemAgentBlockTimeUsKabyLake();

  // Reads the PCU's memory latency data.
  //
  // Returns the raw memory latency data, as it is returned by the PCU firmware.
  // Each entry in the returned array represents a memory latency level, in
  // microseconds. The data may have to be adjusted based on the display engine
  // hardware and on extra information from the memory controller about the
  // installed DRAM.
  //
  // Returns ZX_ERR_IO_MISSED_DEADLINE if a timeout occurs while communicating
  // with the the PCU firmware. Returns ZX_ERR_IO_REFUSED if the PCU firmware
  // reports an error. In either case, the display engine's planes cannot be
  // used safely.
  zx::result<std::array<uint8_t, 8>> GetRawMemoryLatencyDataUs();

  // Reads MemSS (Memory Subsystem) information from the PCU.
  //
  // Returns ZX_ERR_IO_MISSED_DEADLINE if a timeout occurs while communicating
  // with the the PCU firmware. Returns ZX_ERR_IO_REFUSED if the PCU firmware
  // reports an error. In either case, SAGV (System Agent Geyserville) cannot be
  // enabled safely.
  zx::result<MemorySubsystemInfo> GetMemorySubsystemInfoTigerLake();

  static ScopedValueChange<int> OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(
      int timeout_us);
  static ScopedValueChange<int> OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(
      int timeout_us);

 private:
  fdf::MmioBuffer* mmio_buffer_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_POWER_CONTROLLER_H_
