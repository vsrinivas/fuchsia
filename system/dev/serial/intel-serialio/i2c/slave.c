// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <intel-serialio/reg.h>
#include <magenta/types.h>
#include <magenta/device/i2c.h>
#include <magenta/listnode.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "controller.h"
#include "slave.h"

// Time out after 2 seconds.
static const mx_duration_t timeout_ns = MX_SEC(2);

//TODO We should be using interrupts during long operations, but
//the plumbing isn't all there for that apparently.
#define DO_UNTIL(condition, action, poll_interval)                            \
    ({                                                                        \
        const mx_time_t deadline = mx_deadline_after(timeout_ns);             \
        int wait_for_condition_value;                                         \
        while (!(wait_for_condition_value = !!(condition))) {                 \
            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);                  \
            if (now >= deadline)                                              \
                break;                                                        \
            if (poll_interval)                                                \
                mx_nanosleep(mx_deadline_after(poll_interval));               \
            {action;}                                                         \
        }                                                                     \
        wait_for_condition_value;                                             \
    })

#define WAIT_FOR(condition, poll_interval) DO_UNTIL(condition, , poll_interval)

// This is a controller implementation constant.  This value is likely lower
// than reality, but it is a conservative choice.
// TODO(teisenbe): Discover this/look it up from a table
const uint32_t kRxFifoDepth = 8;

// Implement the functionality of the i2c slave devices.

static int bus_is_idle(intel_serialio_i2c_device_t *controller) {
    uint32_t i2c_sta = *REG32(&controller->regs->i2c_sta);
    return !(i2c_sta & (0x1 << I2C_STA_CA)) &&
           (i2c_sta & (0x1 << I2C_STA_TFCE));
}

static int stop_detected(intel_serialio_i2c_device_t *controller) {
    return *REG32(&controller->regs->raw_intr_stat) &
           (0x1 << INTR_STOP_DETECTION);
}

static int rx_fifo_empty(intel_serialio_i2c_device_t *controller) {
    return !(*REG32(&controller->regs->i2c_sta) & (0x1 << I2C_STA_RFNE));
}

static mx_status_t intel_serialio_i2c_slave_transfer(
    intel_serialio_i2c_slave_device_t* slave, i2c_slave_segment_t *segments, int segment_count) {
    mx_status_t status = MX_OK;

    for (int i = 0; i < segment_count; i++) {
        if (segments[i].type != I2C_SEGMENT_TYPE_READ &&
            segments[i].type != I2C_SEGMENT_TYPE_WRITE) {
            status = MX_ERR_INVALID_ARGS;
            goto transfer_finish_2;
        }
    }

    intel_serialio_i2c_device_t* controller = slave->controller;

    uint32_t ctl_addr_mode_bit;
    uint32_t tar_add_addr_mode_bit;
    if (slave->chip_address_width == I2C_7BIT_ADDRESS) {
        ctl_addr_mode_bit = CTL_ADDRESSING_MODE_7BIT;
        tar_add_addr_mode_bit = TAR_ADD_WIDTH_7BIT;
    } else if (slave->chip_address_width == I2C_10BIT_ADDRESS) {
        ctl_addr_mode_bit = CTL_ADDRESSING_MODE_10BIT;
        tar_add_addr_mode_bit = TAR_ADD_WIDTH_10BIT;
    } else {
        printf("Bad address width.\n");
        status = MX_ERR_INVALID_ARGS;
        goto transfer_finish_2;
    }

    mtx_lock(&slave->controller->mutex);

    if (!WAIT_FOR(bus_is_idle(controller), MX_USEC(50))) {
        status = MX_ERR_TIMED_OUT;
        goto transfer_finish_1;
    }

    // Set the target adress value and width.
    RMWREG32(&controller->regs->ctl, CTL_ADDRESSING_MODE, 1, ctl_addr_mode_bit);
    *REG32(&controller->regs->tar_add) =
        (tar_add_addr_mode_bit << TAR_ADD_WIDTH) |
        (slave->chip_address << TAR_ADD_IC_TAR);

    // Enable the controller.
    RMWREG32(&controller->regs->i2c_en, I2C_EN_ENABLE, 1, 1);

    int last_type = I2C_SEGMENT_TYPE_END;
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
        while (len--) {
            // Build the cmd register value.
            uint32_t cmd = (restart << DATA_CMD_RESTART);
            restart = 0;
            switch (segments->type) {
            case I2C_SEGMENT_TYPE_WRITE:
                // Wait if the TX FIFO is full
                if (!(*REG32(&controller->regs->i2c_sta) & (0x1 << I2C_STA_TFNF))) {
                    status = intel_serialio_i2c_wait_for_tx_empty(controller,
                                                                  mx_deadline_after(timeout_ns));
                    if (status != MX_OK) {
                        goto transfer_finish_1;
                    }
                }
                cmd |= (*buf << DATA_CMD_DAT);
                cmd |= (DATA_CMD_CMD_WRITE << DATA_CMD_CMD);
                buf++;
                break;
            case I2C_SEGMENT_TYPE_READ:
                cmd |= (DATA_CMD_CMD_READ << DATA_CMD_CMD);
                break;
            default:
                // shouldn't be reachable
                printf("invalid i2c segment type: %d\n", segments->type);
                status = MX_ERR_INVALID_ARGS;
                goto transfer_finish_1;
            }

            if (!len && !segment_count) {
                cmd |= (0x1 << DATA_CMD_STOP);
            }

            if (segments->type == I2C_SEGMENT_TYPE_READ) {
                status = intel_serialio_i2c_issue_rx(controller, cmd);
                outstanding_reads++;
            } else if (segments->type == I2C_SEGMENT_TYPE_WRITE) {
                status = intel_serialio_i2c_issue_tx(controller, cmd);
            } else {
                __builtin_trap();
            }
            if (status != MX_OK) {
                goto transfer_finish_1;
            }

            // If this is a read, extract data if it's ready.
            while (outstanding_reads) {
                // If len is > 0 and the queue has more space, we can go queue up more work.
                if (len > 0 && outstanding_reads < kRxFifoDepth) {
                    if (rx_fifo_empty(controller)) {
                        break;
                    }
                } else {
                    if (rx_fifo_empty(controller)) {
                        // If we've issued all of our read requests, make sure
                        // that the FIFO threshold will be crossed when the
                        // reads are ready.
                        uint32_t rx_threshold;
                        intel_serialio_i2c_get_rx_fifo_threshold(controller, &rx_threshold);
                        if (len == 0 && outstanding_reads < rx_threshold) {
                            status = intel_serialio_i2c_set_rx_fifo_threshold(controller,
                                                                              outstanding_reads);
                            if (status != MX_OK) {
                                goto transfer_finish_1;
                            }
                        }

                        // Wait for the FIFO to get some data.
                        status = intel_serialio_i2c_wait_for_rx_full(controller,
                                                                     mx_deadline_after(timeout_ns));
                        if (status != MX_OK) {
                            goto transfer_finish_1;
                        }

                        // Restore the RX threshold in case we changed it
                        status = intel_serialio_i2c_set_rx_fifo_threshold(controller,
                                                                          rx_threshold);
                        if (status != MX_OK) {
                            goto transfer_finish_1;
                        }
                    }
                }

                status = intel_serialio_i2c_read_rx(controller, buf);
                if (status != MX_OK) {
                    goto transfer_finish_1;
                }
                buf++;
                outstanding_reads--;
            }
        }
        if (outstanding_reads != 0) {
            __builtin_trap();
        }

        last_type = segments->type;
        segments++;
    }

    // Clear out the stop detect interrupt signal.
    status = intel_serialio_i2c_wait_for_stop_detect(controller, mx_deadline_after(timeout_ns));
    if (status != MX_OK) {
        goto transfer_finish_1;
    }
    status = intel_serialio_i2c_clear_stop_detect(controller);
    if (status != MX_OK) {
        goto transfer_finish_1;
    }

    if (!WAIT_FOR(bus_is_idle(controller), MX_USEC(50))) {
        status = MX_ERR_TIMED_OUT;
        goto transfer_finish_1;
    }

    // Read the data_cmd register to pull data out of the RX FIFO.
    if (!DO_UNTIL(rx_fifo_empty(controller),
                  *REG32(&controller->regs->data_cmd), 0)) {
        status = MX_ERR_TIMED_OUT;
        goto transfer_finish_1;
    }

transfer_finish_1:
    if (status < 0) {
        intel_serialio_i2c_reset_controller(controller);
    }
    mtx_unlock(&controller->mutex);
transfer_finish_2:
    return status;
}

// Implement the char protocol for the slave devices.

static mx_status_t intel_serialio_i2c_slave_read(
    void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
    intel_serialio_i2c_slave_device_t* slave = ctx;
    i2c_slave_segment_t segment = {
        .type = I2C_SEGMENT_TYPE_READ,
        .buf = buf,
        .len = count,
    };
    mx_status_t status = intel_serialio_i2c_slave_transfer(slave, &segment, 1);
    if (status == MX_OK) {
        *actual = count;
    }
    return status;
}

static mx_status_t intel_serialio_i2c_slave_write(
    void* ctx, const void* buf, size_t count, mx_off_t off, size_t* actual) {
    intel_serialio_i2c_slave_device_t* slave = ctx;
    i2c_slave_segment_t segment = {
        .type = I2C_SEGMENT_TYPE_WRITE,
        .buf = (void*)buf,
        .len = count,
    };
    mx_status_t status = intel_serialio_i2c_slave_transfer(slave, &segment, 1);
    if (status == MX_OK) {
        *actual = count;
    }
    return status;
}

static mx_status_t intel_serialio_i2c_slave_transfer_ioctl(
    intel_serialio_i2c_slave_device_t* slave, uint32_t op, const void* in_buf, size_t in_len,
    void* out_buf, size_t out_len, size_t* out_actual) {
    mx_status_t status;
    const size_t base_size = sizeof(i2c_slave_ioctl_segment_t);

    size_t read_len = 0;
    size_t write_len = 0;
    int segment_count = 0;
    const i2c_slave_ioctl_segment_t* ioctl_segment = (const i2c_slave_ioctl_segment_t*)in_buf;
    const void* end = (uint8_t*)in_buf + in_len;
    // Check that the inputs and output buffer are valid.
    while ((void*)ioctl_segment < end) {
        if (ioctl_segment->type == I2C_SEGMENT_TYPE_END) {
            // Advance past the segment, which should be the beginning of write
            // data or the end (if there are no writes).
            ioctl_segment++;
            break;
        }
        if ((void*)((uint8_t*)ioctl_segment + base_size) > end) {
            status = MX_ERR_INVALID_ARGS;
            goto slave_transfer_ioctl_finish_2;
        }

        int len = ioctl_segment->len;

        switch (ioctl_segment->type) {
        case I2C_SEGMENT_TYPE_READ:
            read_len += len;
            break;
        case I2C_SEGMENT_TYPE_WRITE:
            write_len += len;
            break;
        }
        ioctl_segment++;
        segment_count++;
    }
    if ((void*)((uint8_t*)ioctl_segment + write_len) != end) {
        status = MX_ERR_INVALID_ARGS;
        goto slave_transfer_ioctl_finish_2;
    }
    if (out_len < read_len) {
        status = MX_ERR_INVALID_ARGS;
        goto slave_transfer_ioctl_finish_2;
    }
    uint8_t* data = (uint8_t*)ioctl_segment;

    // Build a list of segments to transfer.
    i2c_slave_segment_t* segments =
        calloc(segment_count, sizeof(*segments));
    if (!segments) {
        status = MX_ERR_NO_MEMORY;
        goto slave_transfer_ioctl_finish_2;
    }
    i2c_slave_segment_t* cur_segment = segments;
    uintptr_t out_addr = (uintptr_t)out_buf;
    ioctl_segment = (const i2c_slave_ioctl_segment_t*)in_buf;
    for (int i = 0; i < segment_count; i++) {
        int len = ioctl_segment->len;

        switch (ioctl_segment->type) {
        case I2C_SEGMENT_TYPE_READ:
            cur_segment->type = I2C_SEGMENT_TYPE_READ;
            cur_segment->len = len;
            cur_segment->buf = (uint8_t*)out_addr;
            out_addr += len;
            break;
        case I2C_SEGMENT_TYPE_WRITE:
            cur_segment->type = I2C_SEGMENT_TYPE_WRITE;
            cur_segment->len = len;
            cur_segment->buf = data;
            data += len;
            break;
        default:
            // invalid segment type
            status = MX_ERR_INVALID_ARGS;
            goto slave_transfer_ioctl_finish_1;
            break;
        }

        cur_segment++;
        ioctl_segment++;
    }

    status = intel_serialio_i2c_slave_transfer(slave, segments, segment_count);
    if (status == MX_OK) {
        *out_actual = read_len;
    }

slave_transfer_ioctl_finish_1:
    free(segments);
slave_transfer_ioctl_finish_2:
    return status;
}

static mx_status_t intel_serialio_i2c_slave_ioctl(
    void* ctx, uint32_t op, const void* in_buf, size_t in_len,
    void* out_buf, size_t out_len, size_t* out_actual) {
    intel_serialio_i2c_slave_device_t* slave = ctx;
    switch (op) {
    case IOCTL_I2C_SLAVE_TRANSFER:
        return intel_serialio_i2c_slave_transfer_ioctl(
            slave, op, in_buf, in_len, out_buf, out_len, out_actual);
        break;
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

static void intel_serialio_i2c_slave_release(void* ctx) {
    intel_serialio_i2c_slave_device_t* slave = ctx;
    free(slave);
}

// Implement the device protocol for the slave devices.

mx_protocol_device_t intel_serialio_i2c_slave_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = intel_serialio_i2c_slave_read,
    .write = intel_serialio_i2c_slave_write,
    .ioctl = intel_serialio_i2c_slave_ioctl,
    .release = intel_serialio_i2c_slave_release,
};
