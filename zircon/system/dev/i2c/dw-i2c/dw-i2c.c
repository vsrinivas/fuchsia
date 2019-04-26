// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2cimpl.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <hw/reg.h>
#include <lib/sync/completion.h>
#include <zircon/process.h>
#include <zircon/assert.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <threads.h>

#include "dw-i2c-regs.h"

typedef struct {
    zx_handle_t                     irq_handle;
    zx_handle_t                     event_handle;
    mmio_buffer_t                   regs_iobuff;
    zx_duration_t                   timeout;

    uint32_t                        tx_fifo_depth;
    uint32_t                        rx_fifo_depth;
} i2c_dw_dev_t;

typedef struct {
    pdev_protocol_t pdev;
    zx_device_t* zxdev;
    i2c_dw_dev_t* i2c_devs;
    size_t i2c_dev_count;
} i2c_dw_t;

static zx_status_t i2c_dw_read(i2c_dw_dev_t* dev, uint8_t *buff, uint32_t len, bool stop);
static zx_status_t i2c_dw_write(i2c_dw_dev_t* dev, const uint8_t *buff, uint32_t len, bool stop);
static zx_status_t i2c_dw_set_slave_addr(i2c_dw_dev_t* dev, uint16_t addr);

zx_status_t i2c_dw_dumpstate(i2c_dw_dev_t* dev) {
    zxlogf(INFO, "########################\n");
    zxlogf(INFO, "%s\n", __FUNCTION__);
    zxlogf(INFO, "########################\n");
    zxlogf(INFO, "DW_I2C_ENABLE_STATUS = \t0x%x\n", I2C_DW_READ32(DW_I2C_ENABLE_STATUS));
    zxlogf(INFO, "DW_I2C_ENABLE = \t0x%x\n", I2C_DW_READ32(DW_I2C_ENABLE));
    zxlogf(INFO, "DW_I2C_CON = \t0x%x\n", I2C_DW_READ32(DW_I2C_CON));
    zxlogf(INFO, "DW_I2C_TAR = \t0x%x\n", I2C_DW_READ32(DW_I2C_TAR));
    zxlogf(INFO, "DW_I2C_HS_MADDR = \t0x%x\n", I2C_DW_READ32(DW_I2C_HS_MADDR));
    zxlogf(INFO, "DW_I2C_SS_SCL_HCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_SS_SCL_HCNT));
    zxlogf(INFO, "DW_I2C_SS_SCL_LCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_SS_SCL_LCNT));
    zxlogf(INFO, "DW_I2C_FS_SCL_HCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_FS_SCL_HCNT));
    zxlogf(INFO, "DW_I2C_FS_SCL_LCNT = \t0x%x\n", I2C_DW_READ32(DW_I2C_FS_SCL_LCNT));
    zxlogf(INFO, "DW_I2C_INTR_MASK = \t0x%x\n", I2C_DW_READ32(DW_I2C_INTR_MASK));
    zxlogf(INFO, "DW_I2C_RAW_INTR_STAT = \t0x%x\n", I2C_DW_READ32(DW_I2C_RAW_INTR_STAT));
    zxlogf(INFO, "DW_I2C_RX_TL = \t0x%x\n", I2C_DW_READ32(DW_I2C_RX_TL));
    zxlogf(INFO, "DW_I2C_TX_TL = \t0x%x\n", I2C_DW_READ32(DW_I2C_TX_TL));
    zxlogf(INFO, "DW_I2C_STATUS = \t0x%x\n", I2C_DW_READ32(DW_I2C_STATUS));
    zxlogf(INFO, "DW_I2C_TXFLR = \t0x%x\n", I2C_DW_READ32(DW_I2C_TXFLR));
    zxlogf(INFO, "DW_I2C_RXFLR = \t0x%x\n", I2C_DW_READ32(DW_I2C_RXFLR));
    zxlogf(INFO, "DW_I2C_COMP_PARAM_1 = \t0x%x\n", I2C_DW_READ32(DW_I2C_COMP_PARAM_1));
    zxlogf(INFO, "DW_I2C_TX_ABRT_SOURCE = \t0x%x\n", I2C_DW_READ32(DW_I2C_TX_ABRT_SOURCE));
    return ZX_OK;
}

static zx_status_t i2c_dw_enable_wait(i2c_dw_dev_t* dev, bool enable) {
    int max_poll = 100;
    int poll = 0;

    // set enable bit to 0
    I2C_DW_SET_BITS32(DW_I2C_ENABLE, DW_I2C_ENABLE_ENABLE_START, DW_I2C_ENABLE_ENABLE_BITS, enable);

    do {
        if (I2C_DW_GET_BITS32(DW_I2C_ENABLE_STATUS, DW_I2C_ENABLE_STATUS_EN_START,
                                                    DW_I2C_ENABLE_STATUS_EN_BITS) == enable) {
            // we are done. exit
            return ZX_OK;
        }
        // sleep 10 times the signaling period for the highest i2c transfer speed (400K) ~25uS
        usleep(25);
    } while (poll++ < max_poll);

    zxlogf(ERROR, "%s: Could not %s I2C contoller! DW_I2C_ENABLE_STATUS = 0x%x\n",
                                                            __FUNCTION__,
                                                            enable? "enable" : "disable",
                                                            I2C_DW_READ32(DW_I2C_ENABLE_STATUS));
    i2c_dw_dumpstate(dev);

    return ZX_ERR_TIMED_OUT;
}

static zx_status_t i2c_dw_enable(i2c_dw_dev_t* dev) {
    return i2c_dw_enable_wait(dev, I2C_ENABLE);
    return ZX_OK;
}

static void i2c_dw_clear_interrupts(i2c_dw_dev_t* dev) {
    I2C_DW_READ32(DW_I2C_CLR_INTR); // reading this register will clear all the interrupts
}

static void i2c_dw_disable_interrupts(i2c_dw_dev_t* dev) {
    I2C_DW_WRITE32(DW_I2C_INTR_MASK, 0);
}

static void i2c_dw_enable_interrupts(i2c_dw_dev_t* dev, uint32_t flag) {
    I2C_DW_WRITE32(DW_I2C_INTR_MASK, flag);
}

static zx_status_t i2c_dw_disable(i2c_dw_dev_t* dev) {
    return i2c_dw_enable_wait(dev, I2C_DISABLE);
}

static zx_status_t i2c_dw_wait_event(i2c_dw_dev_t* dev, uint32_t sig_mask) {
    uint32_t    observed;
    zx_time_t   deadline = zx_deadline_after(dev->timeout);

    sig_mask |= I2C_ERROR_SIGNAL;

    zx_status_t status = zx_object_wait_one(dev->event_handle, sig_mask, deadline, &observed);

    if (status != ZX_OK) {
        return status;
    }

    zx_object_signal(dev->event_handle, observed, 0);

    if (observed & I2C_ERROR_SIGNAL) {
        return ZX_ERR_TIMED_OUT;
    }

    return ZX_OK;
}

// Thread to handle interrupts
static int i2c_dw_irq_thread(void* arg) {
    i2c_dw_dev_t* dev = (i2c_dw_dev_t*)arg;
    zx_status_t status;

    while (1) {
        status = zx_interrupt_wait(dev->irq_handle, NULL);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: irq wait failed, retcode = %d\n", __FUNCTION__, status);
            continue;
        }

        uint32_t reg = I2C_DW_READ32(DW_I2C_RAW_INTR_STAT);
        if (reg & DW_I2C_INTR_TX_ABRT) {
            // some sort of error has occurred. figure it out
            i2c_dw_dumpstate(dev);
            zx_object_signal(dev->event_handle, 0, I2C_ERROR_SIGNAL);
            zxlogf(ERROR, "i2c: error on bus\n");
        } else {
            zx_object_signal(dev->event_handle, 0, I2C_TXN_COMPLETE_SIGNAL);
        }
        i2c_dw_clear_interrupts(dev);
        i2c_dw_disable_interrupts(dev);
    }

    return ZX_OK;
}

static zx_status_t i2c_dw_transact(void* ctx, uint32_t bus_id, const i2c_impl_op_t* rws,
                                   size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (rws[i].data_size > I2C_DW_MAX_TRANSFER) {
            return ZX_ERR_OUT_OF_RANGE;
        }
    }

    i2c_dw_t* i2c = ctx;

    if (bus_id >= i2c->i2c_dev_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    i2c_dw_dev_t* dev = &i2c->i2c_devs[bus_id];

    if (count == 0) {
        return ZX_OK;
    }
    for (i = 1; i < count; ++i) {
        if (rws[i].address != rws[0].address) {
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    i2c_dw_set_slave_addr(dev, rws[0].address);
    i2c_dw_enable(dev);
    i2c_dw_disable_interrupts(dev);
    i2c_dw_clear_interrupts(dev);

    zx_status_t status = ZX_OK;
    for (i = 0; i < count; ++i) {
        if (rws[i].is_read) {
            status = i2c_dw_read(dev, rws[i].data_buffer, rws[i].data_size, rws[i].stop);
        } else {
            status = i2c_dw_write(dev, rws[i].data_buffer, rws[i].data_size, rws[i].stop);
        }
        if (status != ZX_OK) {
            return status; // TODO(andresoportus) release the bus
        }
    }

    i2c_dw_disable_interrupts(dev);
    i2c_dw_clear_interrupts(dev);
    i2c_dw_disable(dev);

    return status;
}

static zx_status_t i2c_dw_set_bitrate(void* ctx, uint32_t bus_id, uint32_t bitrate) {
    // TODO: Can't implement due to lack of HI3660 documentation
    return ZX_ERR_NOT_SUPPORTED;
}

static uint32_t i2c_dw_get_bus_count(void* ctx) {
    i2c_dw_t* i2c = ctx;

    return i2c->i2c_dev_count;
}

static zx_status_t i2c_dw_get_max_transfer_size(void* ctx, uint32_t bus_id, size_t* out_size) {
    *out_size = I2C_DW_MAX_TRANSFER;
    return ZX_OK;
}

static zx_status_t i2c_dw_set_slave_addr(i2c_dw_dev_t* dev, uint16_t addr) {
    addr &= 0x7f; // support 7bit for now
    uint32_t reg = I2C_DW_READ32(DW_I2C_TAR);
    reg = I2C_DW_SET_MASK(reg, DW_I2C_TAR_TAR_START, DW_I2C_TAR_TAR_BITS, addr);
    reg = I2C_DW_SET_MASK(reg, DW_I2C_TAR_10BIT_START, DW_I2C_TAR_10BIT_BITS, 0);
    I2C_DW_WRITE32(DW_I2C_TAR, reg);
    return ZX_OK;
}

static zx_status_t i2c_dw_read(i2c_dw_dev_t* dev, uint8_t *buff, uint32_t len, bool stop) {
     uint32_t rx_limit;

    ZX_DEBUG_ASSERT(len <= I2C_DW_MAX_TRANSFER);
    rx_limit = dev->rx_fifo_depth - I2C_DW_READ32(DW_I2C_RXFLR);
    ZX_DEBUG_ASSERT(len <= rx_limit);

    // set threshold to the number of bytes we want to read - 1
    I2C_DW_SET_BITS32(DW_I2C_RX_TL, DW_I2C_RX_TL_START, DW_I2C_RX_TL_BITS, len-1);

    while (len > 0) {
        uint32_t cmd = 0;
        // send STOP cmd if last byte and stop set
        if (len == 1 && stop) {
            cmd = I2C_DW_SET_MASK(cmd, DW_I2C_DATA_CMD_STOP_START, DW_I2C_DATA_CMD_STOP_BITS, 1);
        }
        I2C_DW_WRITE32(DW_I2C_DATA_CMD, cmd | (1 << DW_I2C_DATA_CMD_CMD_START));
        len--;
    }

    i2c_dw_enable_interrupts(dev, DW_I2C_INTR_READ_INTR_MASK);
    zx_status_t status = i2c_dw_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t avail_read = I2C_DW_READ32(DW_I2C_RXFLR);
    for (uint32_t i = 0; i < avail_read; i++) {
        buff[i] = I2C_DW_GET_BITS32(DW_I2C_DATA_CMD, DW_I2C_DATA_CMD_DAT_START,
                                                                        DW_I2C_DATA_CMD_DAT_BITS);
    }

    return ZX_OK;
}

static zx_status_t i2c_dw_write(i2c_dw_dev_t* dev, const uint8_t *buff, uint32_t len, bool stop) {
    uint32_t tx_limit;

    ZX_DEBUG_ASSERT(len <= I2C_DW_MAX_TRANSFER);
    tx_limit = dev->tx_fifo_depth - I2C_DW_READ32(DW_I2C_TXFLR);
    ZX_DEBUG_ASSERT(len <= tx_limit);
    while (len > 0) {
        uint32_t cmd = 0;
        // send STOP cmd if last byte and stop set
        if (len == 1 && stop) {
            cmd = I2C_DW_SET_MASK(cmd, DW_I2C_DATA_CMD_STOP_START, DW_I2C_DATA_CMD_STOP_BITS, 1);
        }
        I2C_DW_WRITE32(DW_I2C_DATA_CMD, cmd | *buff++);
        len--;
    }

    // at this point, we have to wait until all data has been transmitted.
    i2c_dw_enable_interrupts(dev, DW_I2C_INTR_DEFAULT_INTR_MASK);
    zx_status_t status = i2c_dw_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
    if (status != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

static zx_status_t i2c_dw_host_init(i2c_dw_dev_t* dev) {
    uint32_t dw_comp_type;
    uint32_t regval;

    // Make sure we are truly running on a DesignWire IP
    dw_comp_type = I2C_DW_READ32(DW_I2C_COMP_TYPE);

    if (dw_comp_type != I2C_DW_COMP_TYPE_NUM) {
        zxlogf(ERROR, "%s: Incompatible IP Block detected. Expected = 0x%x, Actual = 0x%x\n",
            __FUNCTION__, I2C_DW_COMP_TYPE_NUM, dw_comp_type);

        return ZX_ERR_NOT_SUPPORTED;
    }

    // read the various capabilities of the component
    dev->tx_fifo_depth = I2C_DW_GET_BITS32(DW_I2C_COMP_PARAM_1,
                                                        DW_I2C_COMP_PARAM_1_TXFIFOSZ_START,
                                                        DW_I2C_COMP_PARAM_1_TXFIFOSZ_BITS);
    dev->rx_fifo_depth = I2C_DW_GET_BITS32(DW_I2C_COMP_PARAM_1,
                                                        DW_I2C_COMP_PARAM_1_RXFIFOSZ_START,
                                                        DW_I2C_COMP_PARAM_1_RXFIFOSZ_BITS);

    /* I2C Block Initialization based on DW_apb_i2c_databook Section 7.3 */

    // Disable I2C Block
    i2c_dw_disable(dev);

    // Configure the controller:
    // - Slave Disable
    regval = 0;
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_SLAVE_DIS_START,
                                                        DW_I2C_CON_SLAVE_DIS_BITS,
                                                        I2C_ENABLE);

    // - Enable restart mode
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_RESTART_EN_START,
                                                        DW_I2C_CON_RESTART_EN_BITS,
                                                        I2C_ENABLE);

    // - Set 7-bit address modeset
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_10BITADDRSLAVE_START,
                                                        DW_I2C_CON_10BITADDRSLAVE_BITS,
                                                        I2C_7BIT_ADDR);
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_10BITADDRMASTER_START,
                                                        DW_I2C_CON_10BITADDRMASTER_BITS,
                                                        I2C_7BIT_ADDR);

    // - Set speed to fast, master enable
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_SPEED_START,
                                                        DW_I2C_CON_SPEED_BITS,
                                                        I2C_FAST_MODE);

    // - Set master enable
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_MASTER_MODE_START,
                                                        DW_I2C_CON_MASTER_MODE_BITS,
                                                        I2C_ENABLE);

    // write final mask
    I2C_DW_WRITE32(DW_I2C_CON, regval);

    // Write SS/FS LCNT and HCNT
    // FIXME: for now I am using the magical numbers from android source
    I2C_DW_SET_BITS32(DW_I2C_SS_SCL_HCNT, DW_I2C_SS_SCL_HCNT_START, DW_I2C_SS_SCL_HCNT_BITS, 0x87);
    I2C_DW_SET_BITS32(DW_I2C_SS_SCL_LCNT, DW_I2C_SS_SCL_LCNT_START, DW_I2C_SS_SCL_LCNT_BITS, 0x9f);
    I2C_DW_SET_BITS32(DW_I2C_FS_SCL_HCNT, DW_I2C_FS_SCL_HCNT_START, DW_I2C_FS_SCL_HCNT_BITS, 0x1a);
    I2C_DW_SET_BITS32(DW_I2C_FS_SCL_LCNT, DW_I2C_FS_SCL_LCNT_START, DW_I2C_FS_SCL_LCNT_BITS, 0x32);

    // Setup TX FIFO Thresholds
    I2C_DW_SET_BITS32(DW_I2C_TX_TL, DW_I2C_TX_TL_START, DW_I2C_TX_TL_BITS, 0);

    // disable interrupts
    i2c_dw_disable_interrupts(dev);

    return ZX_OK;
}

static zx_status_t i2c_dw_init(i2c_dw_t* i2c, uint32_t index) {
    zx_status_t status;

    i2c_dw_dev_t* device = &i2c->i2c_devs[index];

    device->timeout = ZX_SEC(10);

    status = pdev_map_mmio_buffer(&i2c->pdev, index, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   &device->regs_iobuff);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer failed %d\n", __FUNCTION__, status);
        goto init_fail;
    }

    status = pdev_get_interrupt(&i2c->pdev, index, 0, &device->irq_handle);
    if (status != ZX_OK) {
        goto init_fail;
    }

    status = zx_event_create(0, &device->event_handle);
    if (status != ZX_OK) {
        goto init_fail;
    }

    // initialize i2c host controller
    status = i2c_dw_host_init(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to initialize i2c host controller %d", __FUNCTION__, status);
        goto init_fail;
    }

    thrd_t irq_thread;
    thrd_create_with_name(&irq_thread, i2c_dw_irq_thread, device, "i2c_dw_irq_thread");

    return ZX_OK;

init_fail:
    if (device) {
        mmio_buffer_release(&device->regs_iobuff);
        if (device->event_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(device->event_handle);
        }
        if (device->irq_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(device->irq_handle);
        }
        free(device);
    }
    return status;
}

static i2c_impl_protocol_ops_t i2c_ops = {
    .get_bus_count = i2c_dw_get_bus_count,
    .get_max_transfer_size = i2c_dw_get_max_transfer_size,
    .set_bitrate = i2c_dw_set_bitrate,
    .transact = i2c_dw_transact,
};

static zx_protocol_device_t i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

static zx_status_t dw_i2c_bind(void* ctx, zx_device_t* parent) {
 printf("dw_i2c_bind\n");
    zx_status_t status;

    i2c_dw_t* i2c = calloc(1, sizeof(i2c_dw_t));
    if (!i2c) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &i2c->pdev)) != ZX_OK) {
        zxlogf(ERROR, "dw_i2c_bind: ZX_PROTOCOL_PDEV not available\n");
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&i2c->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dw_i2c_bind: pdev_get_device_info failed\n");
        goto fail;
    }

    if (info.mmio_count != info.irq_count) {
        zxlogf(ERROR, "dw_i2c_bind: mmio_count %u does not matchirq_count %u\n",
               info.mmio_count, info.irq_count);
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    i2c->i2c_devs = calloc(info.mmio_count, sizeof(i2c_dw_dev_t));
    if (!i2c->i2c_devs) {
        free(i2c);
        return ZX_ERR_NO_MEMORY;
    }
    i2c->i2c_dev_count = info.mmio_count;

    for (uint32_t i = 0; i < i2c->i2c_dev_count; i++) {
        zx_status_t status = i2c_dw_init(i2c, i);
        if (status != ZX_OK) {
            zxlogf(ERROR, "dw_i2c_bind: dw_i2c_dev_init failed: %d\n", status);
            goto fail;
        }
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dw-i2c",
        .ctx = i2c,
        .ops = &i2c_device_proto,
        .proto_id = ZX_PROTOCOL_I2C_IMPL,
        .proto_ops = &i2c_ops,
    };

    status = device_add(parent, &args, &i2c->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dw_i2c_bind: device_add failed\n");
        goto fail;
    }

    return ZX_OK;

fail:
    return status;
}

static zx_driver_ops_t dw_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dw_i2c_bind,
};

ZIRCON_DRIVER_BEGIN(dw_i2c, dw_i2c_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DW_I2C),
ZIRCON_DRIVER_END(dw_i2c)
