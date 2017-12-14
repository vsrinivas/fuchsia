// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <hw/reg.h>
#include <threads.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <threads.h>
#include <zircon/process.h>
#include <zircon/assert.h>
#include <soc/hi3660/i2c-dw.h>

typedef struct {
    i2c_dw_port_t   port;
    zx_paddr_t      base_phys;
    uint32_t        irqnum;
} i2c_dw_dev_desc_t;

//These are specific to the HI3660, if this driver gets used on another
// SoC with DesignWare, these will change.
// TODO: Do not hardcode these values. Pass it via some metadata.
static i2c_dw_dev_desc_t i2c_devs[3] = {
    {.port = DW_I2C_0, .base_phys = MMIO_I2C0_BASE, .irqnum = IRQ_IOMCU_I2C0},
    {.port = DW_I2C_1, .base_phys = MMIO_I2C1_BASE, .irqnum = IRQ_IOMCU_I2C1},
    {.port = DW_I2C_2, .base_phys = MMIO_I2C2_BASE, .irqnum = IRQ_IOMCU_I2C2},
};

static inline i2c_dw_dev_desc_t* get_i2c_dev(i2c_dw_port_t portnum) {
    for (uint32_t i = 0; i < countof(i2c_devs); i++) {
        if (i2c_devs[i].port == portnum) {
            return &i2c_devs[i];
        }
    }
    return NULL;
}

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

static i2c_dw_txn_t* i2c_dw_get_txn(i2c_dw_connection_t* conn) {
    mtx_lock(&conn->dev->txn_mutex);
    i2c_dw_txn_t* txn;
    txn = list_remove_head_type(&conn->dev->free_txn_list, i2c_dw_txn_t, node);

    if (txn == NULL) {
        txn = calloc(1, sizeof(i2c_dw_txn_t));
    }

    mtx_unlock(&conn->dev->txn_mutex);

    return txn;
}

static inline void i2c_dw_queue_txn(i2c_dw_connection_t* conn, i2c_dw_txn_t* txn) {
    mtx_lock(&conn->dev->txn_mutex);
    list_add_head(&conn->dev->txn_list, &txn->node);
    mtx_unlock(&conn->dev->txn_mutex);
}

static inline zx_status_t i2c_dw_queue_async(i2c_dw_connection_t* conn, const uint8_t* txbuff,
                                                uint32_t txlen, uint32_t rxlen,
                                                i2c_complete_cb cb, void* cookie) {
    ZX_DEBUG_ASSERT(conn);
    ZX_DEBUG_ASSERT(txlen <= I2C_DW_MAX_TRANSFER);
    ZX_DEBUG_ASSERT(rxlen <= I2C_DW_MAX_TRANSFER);

    i2c_dw_txn_t* txn;
    txn = i2c_dw_get_txn(conn);

    if (txn == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    memcpy(txn->tx_buff, txbuff, txlen);
    txn->tx_len = txlen;
    txn->rx_len = rxlen;
    txn->cb = cb;
    txn->cookie = cookie;
    txn->conn = conn;

    i2c_dw_queue_txn(conn, txn);
    completion_signal(&conn->dev->txn_active);

    return ZX_OK;
}

zx_status_t i2c_dw_wr_async(i2c_dw_connection_t* conn, const uint8_t *buff, uint32_t len,
                            i2c_complete_cb cb, void* cookie) {
    ZX_DEBUG_ASSERT(buff);
    return i2c_dw_queue_async(conn, buff, len, 0, cb, cookie);
}
zx_status_t i2c_dw_rd_async(i2c_dw_connection_t* conn, uint32_t len, i2c_complete_cb cb,
                            void* cookie) {
    return i2c_dw_queue_async(conn, NULL, 0, len, cb, cookie);
}

zx_status_t i2c_dw_wr_rd_async(i2c_dw_connection_t* conn, const uint8_t* txbuff, uint32_t txlen,
                                uint32_t rxlen, i2c_complete_cb cb, void* cookie) {
    ZX_DEBUG_ASSERT(txbuff);
    return i2c_dw_queue_async(conn, txbuff, txlen, rxlen, cb, cookie);
}

static zx_status_t i2c_dw_enable_wait(i2c_dw_dev_t* dev, bool enable) {
    int max_poll = 100;
    int poll = 0;

    // set enable bit to 0
    I2C_DW_SET_BITS32(DW_I2C_ENABLE, DW_I2C_ENABLE_ENABLE_START, DW_I2C_ENABLE_ENABLE_BITS, enable);

    do {
        if (I2C_DW_GET_BITS32(DW_I2C_ENABLE_STATUS, DW_I2C_ENABLE_STATUS_EN_START, DW_I2C_ENABLE_STATUS_EN_BITS) == enable) {
            // we are done. exit
            return ZX_OK;
        }
        // sleep 10 times the signaling period for the highest i2c transfer speed (400K in our case) ~25uS
        usleep(25);
    } while (poll++ < max_poll);

    zxlogf(ERROR, "%s: Could not %s I2C contoller! DW_I2C_ENABLE_STATUS = 0x%x\n", __FUNCTION__,
                                                                                    enable? "enable" : "disable",
                                                                                    I2C_DW_READ32(DW_I2C_ENABLE_STATUS));
    i2c_dw_dumpstate(dev);

    return ZX_ERR_TIMED_OUT;
}

static zx_status_t i2c_dw_enable(i2c_dw_dev_t* dev) {
    return i2c_dw_enable_wait(dev, I2C_ENABLE);
    return ZX_OK;

}

static void i2c_dw_clear_intrrupts(i2c_dw_dev_t* dev) {
    I2C_DW_READ32(DW_I2C_CLR_INTR); // reading this register will clear all the interrupts
}

static void i2c_dw_disable_interrupts(i2c_dw_dev_t* dev) {
    I2C_DW_WRITE32(DW_I2C_INTR_MASK, 0);
}

static void i2c_dw_enable_interrupts(i2c_dw_dev_t* dev) {
    I2C_DW_WRITE32(DW_I2C_INTR_MASK, DW_I2C_INTR_DEFAULT_INTR_MASK);
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

static int i2c_dw_worker_thread (void* arg) {
    i2c_dw_dev_t* dev = arg;
    i2c_dw_txn_t* txn;

    while(1) {
        mtx_lock(&dev->txn_mutex);
        while ((txn = list_remove_tail_type(&dev->txn_list, i2c_dw_txn_t, node)) != NULL) {
            mtx_unlock(&dev->txn_mutex);
            i2c_dw_set_slave_addr(dev, txn->conn->slave_addr);
            i2c_dw_enable(dev);
            i2c_dw_clear_intrrupts(dev);

            if (txn->tx_len > 0) {
                i2c_dw_write(dev, txn->tx_buff, txn->tx_len);
                if ((txn->cb) && (txn->rx_len == 0)) {
                    txn->cb(ZX_OK, NULL, txn->rx_len, txn->cookie);
                }
            }

            if (txn->rx_len > 0) {
                i2c_dw_read(dev, txn->rx_buff, txn->rx_len);
                if (txn->cb) {
                    txn->cb(ZX_OK, txn->rx_buff, txn->rx_len, txn->cookie);
                }
            }

            memset(txn, 0, sizeof(i2c_dw_txn_t));
            mtx_lock(&dev->txn_mutex);
            list_add_head(&dev->free_txn_list, &txn->node);
            i2c_dw_disable_interrupts(dev);
            i2c_dw_clear_intrrupts(dev);
            i2c_dw_disable(dev);
        }
        mtx_unlock(&dev->txn_mutex);

        completion_wait(&dev->txn_active, ZX_TIME_INFINITE);
        completion_reset(&dev->txn_active);
    }
    return ZX_OK;
}

// Thread to handle interrupts
static int i2c_dw_irq_thread(void* arg) {
    i2c_dw_dev_t* dev = (i2c_dw_dev_t*)arg;
    zx_status_t status;

    while (1) {
        status = zx_interrupt_wait(dev->irq_handle);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: irq wait failed, retcode = %d\n", __FUNCTION__, status);
            continue;
        }

        uint32_t reg = I2C_DW_READ32(DW_I2C_RAW_INTR_STAT);
        if (reg & DW_I2C_INTR_TX_ABRT) {
            // some sort of error has occured. figure it out
            i2c_dw_dumpstate(dev);
            zx_object_signal(dev->event_handle, 0, I2C_ERROR_SIGNAL); // signalling might not be enough. need to handle it
            zxlogf(ERROR, "i2c: error on bus\n");
        } else {
            zx_object_signal(dev->event_handle, 0, I2C_TXN_COMPLETE_SIGNAL);
        }
        i2c_dw_clear_intrrupts(dev);
        zx_interrupt_complete(dev->irq_handle);
    }

    return ZX_OK;
}

static zx_status_t i2c_dw_transact(void* ctx, const void* write_buf, size_t write_length,
                                    size_t read_length, i2c_complete_cb complete_cb,
                                    void* cookie) {
    if (read_length > I2C_DW_MAX_TRANSFER || write_length > I2C_DW_MAX_TRANSFER) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    i2c_dw_connection_t* conn = ctx;
    return i2c_dw_wr_rd_async(conn, write_buf, write_length, read_length, complete_cb, cookie);
}

static zx_status_t i2c_dw_set_bitrate(void* ctx, uint32_t bitrate) {
    // TODO: Can't implement due to lack of HI3660 documentation
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t i2c_dw_get_max_transfer_size(void* ctx, size_t* out_size) {
    *out_size = I2C_DW_MAX_TRANSFER;
    return ZX_OK;
}

static void i2c_dw_channel_release(void* ctx) {
    i2c_dw_connection_t* conn = ctx;
    i2c_dw_release(conn);
}

static i2c_channel_ops_t i2c_dw_channel_ops = {
    .transact =                 i2c_dw_transact,
    .set_bitrate =              i2c_dw_set_bitrate,
    .get_max_transfer_size =    i2c_dw_get_max_transfer_size,
    .channel_release =          i2c_dw_channel_release,
};

static zx_status_t i2c_dw_get_channel(void* ctx, uint32_t channel_id, i2c_channel_t* channel) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t i2c_dw_get_channel_by_address(void* ctx, uint32_t bus_id, uint16_t address,
                                                i2c_channel_t* channel) {
    if (bus_id >= HISI_I2C_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }

    i2c_dw_t* i2c = ctx;
    i2c_dw_dev_t* dev = i2c->i2c_devs[bus_id];

    if (dev == NULL) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    i2c_dw_connection_t* connection;
    uint32_t address_bits = 7;
    if ((address & I2C_10_BIT_ADDR_MASK) == I2C_10_BIT_ADDR_MASK) {
        address_bits = 10;
        address &= ~I2C_10_BIT_ADDR_MASK;
    }

    zx_status_t status = i2c_dw_connect(&connection, dev, address, address_bits);
    if (status != ZX_OK) {
        return status;
    }

    channel->ops = &i2c_dw_channel_ops;
    channel->ctx = connection;

    return ZX_OK;
}

zx_status_t i2c_dw_connect(i2c_dw_connection_t** connection,
                            i2c_dw_dev_t* dev,
                            uint32_t i2c_addr,
                            uint32_t num_addr_bits) {

    if ((num_addr_bits != 7) && (num_addr_bits != 10)) {
        return ZX_ERR_INVALID_ARGS;
    }

    i2c_dw_connection_t *conn;

    mtx_lock(&dev->conn_mutex);
    list_for_every_entry(&dev->connections, conn, i2c_dw_connection_t, node) {
        if (conn->slave_addr == i2c_addr) {
            mtx_unlock(&dev->conn_mutex);
            zxlogf(INFO, "i2c slave address already in use\n");
            return ZX_ERR_INVALID_ARGS;
        }
    }

    conn = calloc(1, sizeof(i2c_dw_connection_t));
    if (conn == NULL) {
        mtx_unlock(&dev->conn_mutex);
        return ZX_ERR_NO_MEMORY;
    }

    conn->slave_addr = i2c_addr;
    conn->addr_bits = num_addr_bits;
    conn->dev = dev;

    list_add_head(&dev->connections, &conn->node);
    mtx_unlock(&dev->conn_mutex);

    zxlogf(INFO, "Added connection for channel %x\n",i2c_addr);
    *connection = conn;
    return ZX_OK;
}

zx_status_t i2c_dw_release(i2c_dw_connection_t* conn) {
    mtx_lock(&conn->dev->conn_mutex);
    list_delete(&conn->node);
    mtx_unlock(&conn->dev->conn_mutex);
    free(conn);

    return ZX_OK;
}

static int i2c_dw_bus_not_busy_wait(i2c_dw_dev_t* dev)
{
    int timeout = 20; // 20 ms max timeout

    while( (I2C_DW_GET_BITS32(DW_I2C_STATUS, DW_I2C_STATUS_ACTIVITY_START, DW_I2C_STATUS_ACTIVITY_BITS) == I2C_ACTIVE) && timeout--) {
        usleep(1000);
    }

    if (timeout <= 0) {
        zxlogf(ERROR, "%s: timeout waiting for bus ready! I2C_STATUS REG = 0x%x\n", __FUNCTION__, I2C_DW_READ32(DW_I2C_STATUS));
        i2c_dw_dumpstate(dev);
        return ZX_ERR_TIMED_OUT;
    }

    return ZX_OK;
}

zx_status_t i2c_dw_set_slave_addr(i2c_dw_dev_t* dev, uint16_t addr) {
    addr &= 0x7f; // support 7bit for now
    uint32_t reg = I2C_DW_READ32(DW_I2C_TAR);
    reg = I2C_DW_SET_MASK(reg, DW_I2C_TAR_TAR_START, DW_I2C_TAR_TAR_BITS, addr);
    reg = I2C_DW_SET_MASK(reg, DW_I2C_TAR_10BIT_START, DW_I2C_TAR_10BIT_BITS, 0);
    I2C_DW_WRITE32(DW_I2C_TAR, reg);
    zxlogf(INFO, "%s: setting slave addr to 0x%x\n", __FUNCTION__, addr);
    return ZX_OK;
}

zx_status_t i2c_dw_read(i2c_dw_dev_t* dev, uint8_t *buff, uint32_t len) {
     uint32_t rx_limit;

    ZX_DEBUG_ASSERT(len <= I2C_DW_MAX_TRANSFER);
    rx_limit = dev->rx_fifo_depth - I2C_DW_READ32(DW_I2C_RXFLR);
    ZX_DEBUG_ASSERT(len <= rx_limit);

    // set threshold to the number of bytes we want to read - 1
    I2C_DW_SET_BITS32(DW_I2C_RX_TL, DW_I2C_RX_TL_START, DW_I2C_RX_TL_BITS, len-1);

    while (len > 0) {
        uint32_t cmd = 0;
        if (len == 1) {
            // send STOP cmd if last byte
            cmd = I2C_DW_SET_MASK(cmd, DW_I2C_DATA_CMD_STOP_START, DW_I2C_DATA_CMD_STOP_BITS, 1);
        }
        I2C_DW_WRITE32(DW_I2C_DATA_CMD, cmd | (1 << DW_I2C_DATA_CMD_CMD_START));
        len--;
    }

    i2c_dw_enable_interrupts(dev);
    zx_status_t status = i2c_dw_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t avail_read = I2C_DW_READ32(DW_I2C_RXFLR);
    for (uint32_t i = 0; i < avail_read; i++) {
        buff[i] = I2C_DW_GET_BITS32(DW_I2C_DATA_CMD, DW_I2C_DATA_CMD_DAT_START, DW_I2C_DATA_CMD_DAT_BITS);
    }

    return ZX_OK;
}

zx_status_t i2c_dw_write(i2c_dw_dev_t* dev, uint8_t *buff, uint32_t len) {
    uint32_t tx_limit;

    ZX_DEBUG_ASSERT(len <= I2C_DW_MAX_TRANSFER);
    tx_limit = dev->tx_fifo_depth - I2C_DW_READ32(DW_I2C_TXFLR);
    ZX_DEBUG_ASSERT(len <= tx_limit);

    while (len > 0) {
        uint32_t cmd = 0;
        if (len == 1) {
            // send STOP cmd if last byte
            cmd = I2C_DW_SET_MASK(cmd, DW_I2C_DATA_CMD_STOP_START, DW_I2C_DATA_CMD_STOP_BITS, 1);
        }
        I2C_DW_WRITE32(DW_I2C_DATA_CMD, cmd | *buff++);
        len--;
    }

    // at this point, we have to wait until all data has been transmitted.
    i2c_dw_enable_interrupts(dev);
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

    // read the various capabitlies of the component
    dev->tx_fifo_depth = I2C_DW_GET_BITS32(DW_I2C_COMP_PARAM_1, DW_I2C_COMP_PARAM_1_TXFIFOSZ_START, DW_I2C_COMP_PARAM_1_TXFIFOSZ_BITS);
    dev->rx_fifo_depth = I2C_DW_GET_BITS32(DW_I2C_COMP_PARAM_1, DW_I2C_COMP_PARAM_1_RXFIFOSZ_START, DW_I2C_COMP_PARAM_1_RXFIFOSZ_BITS);

    /* I2C Block Initialization based on DW_apb_i2c_databook Section 7.3 */

    // Disable I2C Block
    i2c_dw_disable(dev);

    // Configure the controller:
    // - Slave Disable
    regval = 0;
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_SLAVE_DIS_START, DW_I2C_CON_SLAVE_DIS_BITS, I2C_ENABLE);

    // - Enable restart mode
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_RESTART_EN_START, DW_I2C_CON_RESTART_EN_BITS, I2C_ENABLE);

    // - Set 7-bit address modeset
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_10BITADDRSLAVE_START, DW_I2C_CON_10BITADDRSLAVE_BITS, I2C_7BIT_ADDR);
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_10BITADDRMASTER_START, DW_I2C_CON_10BITADDRMASTER_BITS, I2C_7BIT_ADDR);

    // - Set speed to fast, master enable
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_SPEED_START, DW_I2C_CON_SPEED_BITS, I2C_FAST_MODE);

    // - Set master enable
    regval = I2C_DW_SET_MASK(regval, DW_I2C_CON_MASTER_MODE_START, DW_I2C_CON_MASTER_MODE_BITS, I2C_ENABLE);

    // write ifnal mask
    I2C_DW_WRITE32(DW_I2C_CON, regval);

    // Write SS/FS LCNT and HCNT
    // FIXME: for now I am using the magical numbers from android source
    I2C_DW_SET_BITS32(DW_I2C_SS_SCL_HCNT, DW_I2C_SS_SCL_HCNT_START, DW_I2C_SS_SCL_HCNT_BITS, 0x87);
    I2C_DW_SET_BITS32(DW_I2C_SS_SCL_LCNT, DW_I2C_SS_SCL_LCNT_START, DW_I2C_SS_SCL_LCNT_BITS, 0x9f);
    I2C_DW_SET_BITS32(DW_I2C_FS_SCL_HCNT, DW_I2C_FS_SCL_HCNT_START, DW_I2C_FS_SCL_HCNT_BITS, 0x1a);
    I2C_DW_SET_BITS32(DW_I2C_FS_SCL_LCNT, DW_I2C_FS_SCL_LCNT_START, DW_I2C_FS_SCL_LCNT_BITS, 0x32);

    // Setup TX FIFO Thresholds
    I2C_DW_SET_BITS32(DW_I2C_TX_TL, DW_I2C_TX_TL_START, DW_I2C_TX_TL_BITS, dev->tx_fifo_depth>>1);

    // disable interrupts
    i2c_dw_disable_interrupts(dev);

    return ZX_OK;
}

zx_status_t i2c_dw_init(i2c_dw_dev_t** device, i2c_dw_port_t portnum) {
    zx_status_t status;
    zx_handle_t resource;

    i2c_dw_dev_desc_t* dev_desc = get_i2c_dev(portnum);
    if (dev_desc == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    *device = calloc(1, sizeof(i2c_dw_dev_t));
    if (*device == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    // Initialize connection lists
    list_initialize(&(*device)->connections);
    list_initialize(&(*device)->txn_list);
    list_initialize(&(*device)->free_txn_list);
    ZX_DEBUG_ASSERT(mtx_init(&(*device)->conn_mutex, mtx_plain) == thrd_success);
    ZX_DEBUG_ASSERT(mtx_init(&(*device)->txn_mutex, mtx_plain) == thrd_success);

    (*device)->txn_active = COMPLETION_INIT;

    (*device)->timeout = ZX_SEC(10);

    resource = get_root_resource();

    status = io_buffer_init_physical(&(*device)->regs_iobuff,
                                        dev_desc->base_phys,
                                        PAGE_SIZE, resource,
                                        ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: io_buffer_init_physical failed %d\n", __FUNCTION__, status);
        goto init_fail;
    }
    (*device)->virt_reg = io_buffer_virt(&(*device)->regs_iobuff);

    status = zx_interrupt_create(resource, dev_desc->irqnum, ZX_INTERRUPT_MODE_LEVEL_HIGH, &(*device)->irq_handle);
    if (status != ZX_OK) {
        goto init_fail;
    }

    status = zx_event_create(0, &(*device)->event_handle);
    if (status != ZX_OK) {
        goto init_fail;
    }

    // initialize i2c host controller
    status = i2c_dw_host_init(*device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to initialize i2c host controller %d", __FUNCTION__, status);
        goto init_fail;
    }

    thrd_t worker_thread;
    thrd_create_with_name(&worker_thread, i2c_dw_worker_thread, *device, "i2c_dw_worker_thread");
    thrd_t irq_thread;
    thrd_create_with_name(&irq_thread, i2c_dw_irq_thread, *device, "i2c_dw_irq_thread");

    return ZX_OK;

init_fail:
    if (*device) {
        io_buffer_release(&(*device)->regs_iobuff);
        if ((*device)->event_handle != ZX_HANDLE_INVALID) {
            zx_handle_close((*device)->event_handle);
        }
        if ((*device)->irq_handle != ZX_HANDLE_INVALID) {
            zx_handle_close((*device)->irq_handle);
        }
        free(*device);
    }
    return status;
}

static i2c_protocol_ops_t i2c_ops = {
    .get_channel =              i2c_dw_get_channel,
    .get_channel_by_address =   i2c_dw_get_channel_by_address,
};

zx_status_t i2c_dw_bus_init(i2c_dw_t* i2c) {
    zx_status_t status;
    status = i2c_dw_init(&i2c->i2c_devs[DW_I2C_0], DW_I2C_0);
    status = i2c_dw_init(&i2c->i2c_devs[DW_I2C_1], DW_I2C_1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: i2c_dw_init failed %d\n", __FUNCTION__, status);
        return status;
    }
    i2c->proto.ops = &i2c_ops;
    i2c->proto.ctx = i2c;
    return ZX_OK;
}

