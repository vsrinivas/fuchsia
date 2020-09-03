// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-i2c-subordinate.h"

#include <fuchsia/hardware/i2c/c/fidl.h>
#include <lib/fit/function.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include "intel-i2c-controller.h"

namespace intel_i2c {

// Time out after 2 seconds.
constexpr zx::duration kTimeout = zx::sec(2);

// TODO We should be using interrupts during long operations, but
// the plumbing isn't all there for that apparently.
bool DoUntil(const fit::function<bool()>& condition, fit::function<void()> action,
             const zx::duration poll_interval) {
  const zx::time deadline = zx::deadline_after(kTimeout);
  bool wait_for_condition_value;
  while (!(wait_for_condition_value = condition())) {
    zx::time now = zx::clock::get_monotonic();
    if (now >= deadline)
      break;
    if (poll_interval != zx::duration(0))
      zx::nanosleep(zx::deadline_after(poll_interval));
    action();
  }
  return wait_for_condition_value;
}

bool WaitFor(const fit::function<bool()>& condition, const zx::duration poll_interval) {
  return DoUntil(condition, nullptr, poll_interval);
}

std::unique_ptr<IntelI2cSubordinate> IntelI2cSubordinate::Create(IntelI2cController* controller,
                                                                 const uint8_t chip_address_width,
                                                                 const uint16_t chip_address,
                                                                 const uint32_t i2c_class) {
  if (chip_address_width != kI2c7BitAddress && chip_address_width != kI2c10BitAddress) {
    zxlogf(ERROR, "Bad address width.");
    return nullptr;
  }

  return std::unique_ptr<IntelI2cSubordinate>(
      new IntelI2cSubordinate(controller, chip_address_width, chip_address, i2c_class));
}

zx_status_t IntelI2cSubordinate::Transfer(const IntelI2cSubordinateSegment* segments,
                                          int segment_count) {
  zx_status_t status = ZX_OK;
  int last_type = fuchsia_hardware_i2c_SegmentType_END;

  for (int i = 0; i < segment_count; i++) {
    if (segments[i].type != fuchsia_hardware_i2c_SegmentType_READ &&
        segments[i].type != fuchsia_hardware_i2c_SegmentType_WRITE) {
      status = ZX_ERR_INVALID_ARGS;
      return status;
    }
  }

  if (!WaitFor(fit::bind_member(controller_, &IntelI2cController::IsBusIdle), zx::usec(50))) {
    status = ZX_ERR_TIMED_OUT;
    return status;
  }

  const uint32_t ctl_addr_mode_bit =
      (chip_address_width_ == kI2c7BitAddress) ? kCtlAddressingMode7Bit : kCtlAddressingMode10Bit;
  const uint32_t tar_add_addr_mode_bit =
      (chip_address_width_ == kI2c7BitAddress) ? kTarAddWidth7Bit : kTarAddWidth10Bit;

  controller_->SetAddressingMode(ctl_addr_mode_bit);
  controller_->SetTargetAddress(tar_add_addr_mode_bit, chip_address_);

  controller_->Enable();

  if (segment_count)
    last_type = segments->type;

  while (segment_count--) {
    int len = segments->len;
    uint8_t* buf = segments->buf;

    // If this segment is in the same direction as the last, inject a
    // restart at its start.
    uint32_t restart = 0;
    if (last_type == segments->type)
      restart = 1;
    size_t outstanding_reads = 0;
    while (len-- || outstanding_reads) {
      // Build the cmd register value.
      uint32_t cmd = (restart << kDataCmdRestart);
      restart = 0;
      switch (segments->type) {
        case fuchsia_hardware_i2c_SegmentType_WRITE:
          // Wait if the TX FIFO is full
          if (controller_->IsTxFifoFull()) {
            status = controller_->WaitForTxEmpty(zx::deadline_after(kTimeout));
            if (status != ZX_OK) {
              return status;
            }
          }
          cmd |= (*buf << kDataCmdDat);
          cmd |= (kDataCmdCmdWrite << kDataCmdCmd);
          buf++;
          break;
        case fuchsia_hardware_i2c_SegmentType_READ:
          cmd |= (kDataCmdCmdRead << kDataCmdCmd);
          break;
        default:
          // shouldn't be reachable
          printf("invalid i2c segment type: %d\n", segments->type);
          status = ZX_ERR_INVALID_ARGS;
          return status;
      }

      if (!len && !segment_count) {
        cmd |= (0x1 << kDataCmdStop);
      }

      if (segments->type == fuchsia_hardware_i2c_SegmentType_READ) {
        status = controller_->IssueRx(cmd);
        outstanding_reads++;
      } else if (segments->type == fuchsia_hardware_i2c_SegmentType_WRITE) {
        status = controller_->IssueTx(cmd);
      } else {
        __builtin_trap();
      }
      if (status != ZX_OK) {
        return status;
      }

      // If its a read then queue up more reads until we hit fifo_depth.
      // (We use fifo_depth - 1 because going to the full fifo_depth
      // causes an overflow interrupt).
      if (outstanding_reads != 0 && len > 0 &&
          outstanding_reads < (size_t)(controller_->GetRxFifoDepth() - 1)) {
        continue;
      }

      uint32_t rx_data_left = controller_->GetRxFifoLevel();
      // If this is a read, extract data if it's ready.
      while (outstanding_reads) {
        while (rx_data_left == 0) {
          // Make sure that the FIFO threshold will be crossed when
          // the reads are ready.
          uint32_t rx_threshold = static_cast<uint32_t>(outstanding_reads);
          status = controller_->SetRxFifoThreshold(rx_threshold);
          if (status != ZX_OK) {
            return status;
          }

          // Clear the RX threshold signal
          status = controller_->FlushRxFullIrq();
          if (status != ZX_OK) {
            return status;
          }

          // Wait for the FIFO to get some data.
          status = controller_->WaitForRxFull(zx::deadline_after(kTimeout));
          if (status != ZX_OK) {
            return status;
          }
          rx_data_left = controller_->GetRxFifoLevel();
        }

        *buf = controller_->ReadRx();
        buf++;
        outstanding_reads--;
        rx_data_left--;
      }
    }
    if (outstanding_reads != 0) {
      __builtin_trap();
    }

    last_type = segments->type;
    segments++;
  }

  // Clear out the stop detect interrupt signal.
  status = controller_->WaitForStopDetect(zx::deadline_after(kTimeout));
  if (status != ZX_OK) {
    return status;
  }
  status = controller_->ClearStopDetect();
  if (status != ZX_OK) {
    return status;
  }

  if (!WaitFor(fit::bind_member(controller_, &IntelI2cController::IsBusIdle), zx::usec(50))) {
    status = ZX_ERR_TIMED_OUT;
    return status;
  }

  // Read the data_cmd register to pull data out of the RX FIFO.
  if (!DoUntil(fit::bind_member(controller_, &IntelI2cController::IsRxFifoEmpty),
               [this](){controller_->ReadRx();}, zx::duration(0))) {
    status = ZX_ERR_TIMED_OUT;
    return status;
  }

  status = controller_->CheckForError();

  return status;
}

}  // namespace intel_i2c
