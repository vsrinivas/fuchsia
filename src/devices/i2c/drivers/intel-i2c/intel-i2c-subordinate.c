// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-i2c-subordinate.h"

#include <fuchsia/hardware/i2c/c/fidl.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include "intel-i2c-controller.h"

#define RMWREG32(addr, startbit, width, val)                                                      \
  MmioWrite32((MmioRead32(addr) & ~(((1 << (width)) - 1) << (startbit))) | ((val) << (startbit)), \
              (addr))

// Time out after 2 seconds.
static const zx_duration_t timeout_ns = ZX_SEC(2);

// TODO We should be using interrupts during long operations, but
// the plumbing isn't all there for that apparently.
#define DO_UNTIL(condition, action, poll_interval)            \
  ({                                                          \
    const zx_time_t deadline = zx_deadline_after(timeout_ns); \
    int wait_for_condition_value;                             \
    while (!(wait_for_condition_value = !!(condition))) {     \
      zx_time_t now = zx_clock_get_monotonic();               \
      if (now >= deadline)                                    \
        break;                                                \
      if (poll_interval)                                      \
        zx_nanosleep(zx_deadline_after(poll_interval));       \
      { action; }                                             \
    }                                                         \
    wait_for_condition_value;                                 \
  })

#define WAIT_FOR(condition, poll_interval) DO_UNTIL(condition, , poll_interval)

// Implement the functionality of the i2c subordinate devices.

static int bus_is_idle(intel_serialio_i2c_device_t* controller) {
  uint32_t i2c_sta = MmioRead32(&controller->regs->i2c_sta);
  return !(i2c_sta & (0x1 << I2C_STA_CA)) && (i2c_sta & (0x1 << I2C_STA_TFCE));
}

static int stop_detected(intel_serialio_i2c_device_t* controller) {
  return (MmioRead32(&controller->regs->raw_intr_stat) & (0x1 << INTR_STOP_DETECTION));
}

static uint32_t rx_fifo_level(intel_serialio_i2c_device_t* controller) {
  return MmioRead32(&controller->regs->rxflr) & 0x1ff;
}
static int rx_fifo_empty(intel_serialio_i2c_device_t* controller) {
  return !(MmioRead32(&controller->regs->i2c_sta) & (0x1 << I2C_STA_RFNE));
}

// Thread safety analysis cannot see the control flow through the
// gotos, and cannot prove that the lock is unheld at return through
// all paths.
zx_status_t intel_serialio_i2c_subordinate_transfer(
    intel_serialio_i2c_subordinate_device_t* subordinate, i2c_subordinate_segment_t* segments,
    int segment_count) TA_NO_THREAD_SAFETY_ANALYSIS {
  zx_status_t status = ZX_OK;

  for (int i = 0; i < segment_count; i++) {
    if (segments[i].type != fuchsia_hardware_i2c_SegmentType_READ &&
        segments[i].type != fuchsia_hardware_i2c_SegmentType_WRITE) {
      status = ZX_ERR_INVALID_ARGS;
      goto transfer_finish_2;
    }
  }

  intel_serialio_i2c_device_t* controller = subordinate->controller;

  uint32_t ctl_addr_mode_bit;
  uint32_t tar_add_addr_mode_bit;
  if (subordinate->chip_address_width == I2C_7BIT_ADDRESS) {
    ctl_addr_mode_bit = CTL_ADDRESSING_MODE_7BIT;
    tar_add_addr_mode_bit = TAR_ADD_WIDTH_7BIT;
  } else if (subordinate->chip_address_width == I2C_10BIT_ADDRESS) {
    ctl_addr_mode_bit = CTL_ADDRESSING_MODE_10BIT;
    tar_add_addr_mode_bit = TAR_ADD_WIDTH_10BIT;
  } else {
    printf("Bad address width.\n");
    status = ZX_ERR_INVALID_ARGS;
    goto transfer_finish_2;
  }

  mtx_lock(&subordinate->controller->mutex);

  if (!WAIT_FOR(bus_is_idle(controller), ZX_USEC(50))) {
    status = ZX_ERR_TIMED_OUT;
    goto transfer_finish_1;
  }

  // Set the target adress value and width.
  RMWREG32(&controller->regs->ctl, CTL_ADDRESSING_MODE, 1, ctl_addr_mode_bit);
  MmioWrite32((tar_add_addr_mode_bit << TAR_ADD_WIDTH) | (subordinate->chip_address << TAR_ADD_IC_TAR),
         &controller->regs->tar_add);

  // Enable the controller.
  RMWREG32(&controller->regs->i2c_en, I2C_EN_ENABLE, 1, 1);

  int last_type = fuchsia_hardware_i2c_SegmentType_END;
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
      uint32_t cmd = (restart << DATA_CMD_RESTART);
      restart = 0;
      switch (segments->type) {
        case fuchsia_hardware_i2c_SegmentType_WRITE:
          // Wait if the TX FIFO is full
          if (!(MmioRead32(&controller->regs->i2c_sta) & (0x1 << I2C_STA_TFNF))) {
            status =
                intel_serialio_i2c_wait_for_tx_empty(controller, zx_deadline_after(timeout_ns));
            if (status != ZX_OK) {
              goto transfer_finish_1;
            }
          }
          cmd |= (*buf << DATA_CMD_DAT);
          cmd |= (DATA_CMD_CMD_WRITE << DATA_CMD_CMD);
          buf++;
          break;
        case fuchsia_hardware_i2c_SegmentType_READ:
          cmd |= (DATA_CMD_CMD_READ << DATA_CMD_CMD);
          break;
        default:
          // shouldn't be reachable
          printf("invalid i2c segment type: %d\n", segments->type);
          status = ZX_ERR_INVALID_ARGS;
          goto transfer_finish_1;
      }

      if (!len && !segment_count) {
        cmd |= (0x1 << DATA_CMD_STOP);
      }

      if (segments->type == fuchsia_hardware_i2c_SegmentType_READ) {
        status = intel_serialio_i2c_issue_rx(controller, cmd);
        outstanding_reads++;
      } else if (segments->type == fuchsia_hardware_i2c_SegmentType_WRITE) {
        status = intel_serialio_i2c_issue_tx(controller, cmd);
      } else {
        __builtin_trap();
      }
      if (status != ZX_OK) {
        goto transfer_finish_1;
      }

      // If its a read then queue up more reads until we hit fifo_depth.
      // (We use fifo_depth - 1 because going to the full fifo_depth
      // causes an overflow interrupt).
      if (outstanding_reads != 0 && len > 0 &&
          outstanding_reads < (size_t)(controller->rx_fifo_depth - 1)) {
        continue;
      }

      uint32_t rx_data_left = rx_fifo_level(controller);
      // If this is a read, extract data if it's ready.
      while (outstanding_reads) {
        while (rx_data_left == 0) {
          // Make sure that the FIFO threshold will be crossed when
          // the reads are ready.
          uint32_t rx_threshold = outstanding_reads;
          status = intel_serialio_i2c_set_rx_fifo_threshold(controller, rx_threshold);
          if (status != ZX_OK) {
            goto transfer_finish_1;
          }

          // Clear the RX threshold signal
          status = intel_serialio_i2c_flush_rx_full_irq(controller);
          if (status != ZX_OK) {
            goto transfer_finish_1;
          }

          // Wait for the FIFO to get some data.
          status = intel_serialio_i2c_wait_for_rx_full(controller, zx_deadline_after(timeout_ns));
          if (status != ZX_OK) {
            goto transfer_finish_1;
          }
          rx_data_left = rx_fifo_level(controller);
        }

        status = intel_serialio_i2c_read_rx(controller, buf);
        if (status != ZX_OK) {
          goto transfer_finish_1;
        }
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
  status = intel_serialio_i2c_wait_for_stop_detect(controller, zx_deadline_after(timeout_ns));
  if (status != ZX_OK) {
    goto transfer_finish_1;
  }
  status = intel_serialio_i2c_clear_stop_detect(controller);
  if (status != ZX_OK) {
    goto transfer_finish_1;
  }

  if (!WAIT_FOR(bus_is_idle(controller), ZX_USEC(50))) {
    status = ZX_ERR_TIMED_OUT;
    goto transfer_finish_1;
  }

  // Read the data_cmd register to pull data out of the RX FIFO.
  if (!DO_UNTIL(rx_fifo_empty(controller), MmioRead32(&controller->regs->data_cmd), 0)) {
    status = ZX_ERR_TIMED_OUT;
    goto transfer_finish_1;
  }

  status = intel_serialio_i2c_check_for_error(controller);
  // fall-through for error processing

transfer_finish_1:
  if (status < 0) {
    intel_serialio_i2c_reset_controller(controller);
  }
  mtx_unlock(&controller->mutex);
transfer_finish_2:
  return status;
}

static zx_status_t intel_serialio_i2c_subordinate_transfer_helper(
    intel_serialio_i2c_subordinate_device_t* subordinate, const void* in_buf, size_t in_len,
    void* out_buf, size_t out_len, size_t* out_actual) {
  zx_status_t status;
  const size_t base_size = sizeof(fuchsia_hardware_i2c_Segment);

  size_t read_len = 0;
  size_t write_len = 0;
  int segment_count = 0;
  const fuchsia_hardware_i2c_Segment* segment = (const fuchsia_hardware_i2c_Segment*)in_buf;
  const void* end = (uint8_t*)in_buf + in_len;
  // Check that the inputs and output buffer are valid.
  while ((void*)segment < end) {
    if (segment->type == fuchsia_hardware_i2c_SegmentType_END) {
      // Advance past the segment, which should be the beginning of write
      // data or the end (if there are no writes).
      segment++;
      break;
    }
    if ((void*)((uint8_t*)segment + base_size) > end) {
      status = ZX_ERR_INVALID_ARGS;
      goto subordinate_transfer_finish_2;
    }

    int len = segment->len;

    switch (segment->type) {
      case fuchsia_hardware_i2c_SegmentType_READ:
        read_len += len;
        break;
      case fuchsia_hardware_i2c_SegmentType_WRITE:
        write_len += len;
        break;
    }
    segment++;
    segment_count++;
  }
  if ((void*)((uint8_t*)segment + write_len) != end) {
    status = ZX_ERR_INVALID_ARGS;
    goto subordinate_transfer_finish_2;
  }
  if (out_len < read_len) {
    status = ZX_ERR_INVALID_ARGS;
    goto subordinate_transfer_finish_2;
  }
  uint8_t* data = (uint8_t*)segment;

  // Build a list of segments to transfer.
  i2c_subordinate_segment_t* segments = calloc(segment_count, sizeof(*segments));
  if (!segments) {
    status = ZX_ERR_NO_MEMORY;
    goto subordinate_transfer_finish_2;
  }
  i2c_subordinate_segment_t* cur_segment = segments;
  uintptr_t out_addr = (uintptr_t)out_buf;
  segment = (const fuchsia_hardware_i2c_Segment*)in_buf;
  for (int i = 0; i < segment_count; i++) {
    int len = segment->len;

    switch (segment->type) {
      case fuchsia_hardware_i2c_SegmentType_READ:
        cur_segment->type = fuchsia_hardware_i2c_SegmentType_READ;
        cur_segment->len = len;
        cur_segment->buf = (uint8_t*)out_addr;
        out_addr += len;
        break;
      case fuchsia_hardware_i2c_SegmentType_WRITE:
        cur_segment->type = fuchsia_hardware_i2c_SegmentType_WRITE;
        cur_segment->len = len;
        cur_segment->buf = data;
        data += len;
        break;
      default:
        // invalid segment type
        status = ZX_ERR_INVALID_ARGS;
        goto subordinate_transfer_finish_1;
        break;
    }

    cur_segment++;
    segment++;
  }

  status = intel_serialio_i2c_subordinate_transfer(subordinate, segments, segment_count);
  if (status == ZX_OK) {
    *out_actual = read_len;
  }

subordinate_transfer_finish_1:
  free(segments);
subordinate_transfer_finish_2:
  return status;
}

zx_status_t intel_serialio_i2c_subordinate_get_irq(
    intel_serialio_i2c_subordinate_device_t* subordinate, zx_handle_t* out) {
  if (subordinate->chip_address == 0xa) {
    zx_handle_t irq;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_status_t status =
        zx_interrupt_create(get_root_resource(), 0x1f, ZX_INTERRUPT_MODE_LEVEL_LOW, &irq);
    if (status != ZX_OK) {
      return status;
    }
    *out = irq;
    return ZX_OK;
  } else if (subordinate->chip_address == 0x49) {
    zx_handle_t irq;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_status_t status =
        zx_interrupt_create(get_root_resource(), 0x33, ZX_INTERRUPT_MODE_LEVEL_LOW, &irq);
    if (status != ZX_OK) {
      return status;
    }
    *out = irq;
    return ZX_OK;
  } else if (subordinate->chip_address == 0x10) {
    // Acer12
    zx_handle_t irq;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_status_t status =
        zx_interrupt_create(get_root_resource(), 0x1f, ZX_INTERRUPT_MODE_LEVEL_LOW, &irq);
    if (status != ZX_OK) {
      return status;
    }
    *out = irq;
    return ZX_OK;
  } else if (subordinate->chip_address == 0x50) {
    zx_handle_t irq;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_status_t status =
        zx_interrupt_create(get_root_resource(), 0x18, ZX_INTERRUPT_MODE_EDGE_LOW, &irq);
    if (status != ZX_OK) {
      return status;
    }
    *out = irq;
    return ZX_OK;
  } else if (subordinate->chip_address == 0x15) {
    zx_handle_t irq;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx_status_t status =
        zx_interrupt_create(get_root_resource(), 0x2b, ZX_INTERRUPT_MODE_EDGE_LOW, &irq);
    if (status != ZX_OK) {
      return status;
    }
    *out = irq;
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

static void intel_serialio_i2c_subordinate_release(void* ctx) {
  intel_serialio_i2c_subordinate_device_t* subordinate = ctx;
  free(subordinate);
}

static zx_status_t fidl_SubordinateTransfer(void* ctx, const unsigned char* in_buf,
                                            long unsigned int in_len, fidl_txn_t* txn) {
  intel_serialio_i2c_subordinate_device_t* subordinate = ctx;
  uint8_t out_data[fuchsia_hardware_i2c_MAX_TRANSFER_SIZE];
  size_t out_actual = 0;
  zx_status_t status = intel_serialio_i2c_subordinate_transfer_helper(
      subordinate, in_buf, in_len, out_data, fuchsia_hardware_i2c_MAX_TRANSFER_SIZE, &out_actual);
  return fuchsia_hardware_i2c_DeviceSubordinateTransfer_reply(txn, status, out_data, out_actual);
}

static fuchsia_hardware_i2c_Device_ops_t fidl_ops = {
    .SubordinateTransfer = fidl_SubordinateTransfer,
};

zx_status_t intel_serialio_i2c_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_i2c_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

// Implement the device protocol for the subordinate devices.

zx_protocol_device_t intel_serialio_i2c_subordinate_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .message = intel_serialio_i2c_message,
    .release = intel_serialio_i2c_subordinate_release,
};
