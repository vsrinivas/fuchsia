// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <bits/limits.h>
#include <ddk/debug.h>
#include <hw/reg.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#include <soc/aml-a113/a113-bus.h>
#include <soc/aml-a113/aml-i2c.h>

typedef struct {
    aml_i2c_port_t port;
    zx_paddr_t     base_phys;
    uint32_t       irqnum;
} aml_i2c_dev_desc_t;

//These are specific to the A113, if this driver gets used with another
// AMLogic SoC then they will most likely be different.
static aml_i2c_dev_desc_t i2c_devs[4] = {
    {.port = AML_I2C_A, .base_phys = 0xffd1f000, .irqnum = (21+32)},
    {.port = AML_I2C_B, .base_phys = 0xffd1e000, .irqnum = (214+32)},
    {.port = AML_I2C_C, .base_phys = 0xffd1d000, .irqnum = (215+32)},
    {.port = AML_I2C_D, .base_phys = 0xffd1c000, .irqnum = (39+32)},
};

static inline aml_i2c_dev_desc_t* get_i2c_dev(aml_i2c_port_t portnum) {
    for (uint32_t i = 0; i < countof(i2c_devs); i++) {
        if (i2c_devs[i].port == portnum) return &i2c_devs[i];
    }
    return NULL;
}

zx_status_t aml_i2c_set_slave_addr(aml_i2c_dev_t *dev, uint16_t addr) {

    addr &= 0x7f;
    uint32_t reg = dev->virt_regs->slave_addr;
    reg = reg & 0xff;
    reg = reg | ((addr << 1) & 0xff);
    dev->virt_regs->slave_addr = reg;

    return ZX_OK;
}

static int aml_i2c_irq_thread(void *arg) {

    aml_i2c_dev_t *dev = arg;
    zx_status_t status;

    while (1) {
        status = zx_interrupt_wait(dev->irq);
        if (status != ZX_OK) {
            zxlogf(ERROR, "i2c: interrupt error\n");
            continue;
        }

        uint32_t reg =  dev->virt_regs->control;
        if (reg & AML_I2C_CONTROL_REG_ERR) {
            zx_object_signal(dev->event, 0, I2C_ERROR_SIGNAL);
            zxlogf(ERROR,"i2c: error on bus\n");
        } else {
            zx_object_signal(dev->event, 0, I2C_TXN_COMPLETE_SIGNAL);
        }
        zx_interrupt_complete(dev->irq);
    }
    return ZX_OK;
}

static int aml_i2c_thread(void *arg) {

    aml_i2c_dev_t *dev = arg;
    aml_i2c_txn_t *txn;

    while (1) {
        mtx_lock(&dev->txn_mutex);
        while ((txn = list_remove_tail_type(&dev->txn_list, aml_i2c_txn_t, node)) != NULL) {
            mtx_unlock(&dev->txn_mutex);
            aml_i2c_set_slave_addr(dev, txn->conn->slave_addr);
            if (txn->tx_len > 0) {
                aml_i2c_write(dev,txn->tx_buff, txn->tx_len);
                if ((txn->cb) && (txn->rx_len == 0)) {
                    txn->cb(ZX_OK, NULL, txn->rx_len, txn->cookie);
                }
            }
            if (txn->rx_len > 0) {
                aml_i2c_read(dev, txn->rx_buff, txn->rx_len);
                if (txn->cb) {
                    txn->cb(ZX_OK, txn->rx_buff, txn->rx_len, txn->cookie);
                }
            }
            memset(txn, 0, sizeof(aml_i2c_txn_t));
            mtx_lock(&dev->txn_mutex);
            list_add_head(&dev->free_txn_list, &txn->node);
            // keep holding mutex for while loop test
        }
        mtx_unlock(&dev->txn_mutex);

        completion_wait(&dev->txn_active, ZX_TIME_INFINITE);
        completion_reset(&dev->txn_active);
    }
    return 0;
}

zx_status_t aml_i2c_dumpstate(aml_i2c_dev_t *dev) {

    printf("control reg      : %08x\n",dev->virt_regs->control);
    printf("slave addr  reg  : %08x\n",dev->virt_regs->slave_addr);
    printf("token list0 reg  : %08x\n",dev->virt_regs->token_list_0);
    printf("token list1 reg  : %08x\n",dev->virt_regs->token_list_1);
    printf("token wdata0     : %08x\n",dev->virt_regs->token_wdata_0);
    printf("token wdata1     : %08x\n",dev->virt_regs->token_wdata_1);
    printf("token rdata0     : %08x\n",dev->virt_regs->token_rdata_0);
    printf("token rdata1     : %08x\n",dev->virt_regs->token_rdata_1);

    return ZX_OK;
}

zx_status_t aml_i2c_start_xfer(aml_i2c_dev_t *dev) {
    //First have to clear the start bit before setting (RTFM)
    dev->virt_regs->control &= ~AML_I2C_CONTROL_REG_START;
    dev->virt_regs->control |= AML_I2C_CONTROL_REG_START;
    return ZX_OK;
}

static aml_i2c_txn_t *aml_i2c_get_txn(aml_i2c_connection_t *conn) {

    mtx_lock(&conn->dev->txn_mutex);
    aml_i2c_txn_t *txn;
    txn = list_remove_head_type(&conn->dev->free_txn_list, aml_i2c_txn_t, node);
    if (!txn) {
        txn = calloc(1, sizeof(aml_i2c_txn_t));
    }
    mtx_unlock(&conn->dev->txn_mutex);
    return txn;
}

static inline void aml_i2c_queue_txn(aml_i2c_connection_t *conn, aml_i2c_txn_t *txn) {
    mtx_lock(&conn->dev->txn_mutex);
    list_add_head(&conn->dev->txn_list, &txn->node);
    mtx_unlock(&conn->dev->txn_mutex);
}

static inline zx_status_t aml_i2c_queue_async(aml_i2c_connection_t *conn, const uint8_t *txbuff,
                                                            uint32_t txlen, uint32_t rxlen,
                                                            i2c_complete_cb cb, void* cookie) {
    ZX_DEBUG_ASSERT(conn);
    ZX_DEBUG_ASSERT(txlen <= AML_I2C_MAX_TRANSFER);
    ZX_DEBUG_ASSERT(rxlen <= AML_I2C_MAX_TRANSFER);

    aml_i2c_txn_t *txn;

    txn = aml_i2c_get_txn(conn);
    if (!txn) return ZX_ERR_NO_MEMORY;

    memcpy(txn->tx_buff, txbuff, txlen);
    txn->tx_len = txlen;
    txn->rx_len = rxlen;
    txn->cb = cb;
    txn->cookie = cookie;
    txn->conn = conn;

    aml_i2c_queue_txn(conn,txn);
    completion_signal(&conn->dev->txn_active);

    return ZX_OK;
}

zx_status_t aml_i2c_rd_async(aml_i2c_connection_t *conn, uint32_t len, i2c_complete_cb cb,
                                                            void* cookie) {

    return aml_i2c_queue_async(conn, NULL, 0, len, cb, cookie);
}

zx_status_t aml_i2c_wr_async(aml_i2c_connection_t *conn, const uint8_t *buff, uint32_t len,
                                                            i2c_complete_cb cb, void* cookie) {

    ZX_DEBUG_ASSERT(buff);
    return aml_i2c_queue_async(conn, buff, len, 0, cb, cookie);
}

zx_status_t aml_i2c_wr_rd_async(aml_i2c_connection_t *conn, const uint8_t *txbuff, uint32_t txlen,
                                                            uint32_t rxlen, i2c_complete_cb cb,
                                                            void* cookie) {

    ZX_DEBUG_ASSERT(txbuff);
    return aml_i2c_queue_async(conn, txbuff, txlen, rxlen, cb, cookie);
}

static zx_status_t aml_i2c_wait_event(aml_i2c_dev_t* dev, uint32_t sig_mask) {

    zx_time_t deadline = zx_deadline_after(dev->timeout);
    uint32_t observed;
    sig_mask |= I2C_ERROR_SIGNAL;
    zx_status_t status = zx_object_wait_one(dev->event, sig_mask, deadline, &observed);
    if (status != ZX_OK) {
        return status;
    }
    zx_object_signal(dev->event,observed,0);
    if (observed & I2C_ERROR_SIGNAL)
        return ZX_ERR_TIMED_OUT;
    return ZX_OK;
}


zx_status_t aml_i2c_write(aml_i2c_dev_t *dev, uint8_t *buff, uint32_t len) {

    //temporary hack, only transactions that can fit in hw buffer
    ZX_DEBUG_ASSERT(len <= AML_I2C_MAX_TRANSFER);

    uint32_t token_num = 0;
    uint64_t token_reg = 0;

    token_reg |= (uint64_t)TOKEN_START << (4*(token_num++));
    token_reg |= (uint64_t)TOKEN_SLAVE_ADDR_WR << (4*(token_num++));

    for (uint32_t i=0; i < len; i++) {
        token_reg |= (uint64_t)TOKEN_DATA << (4*(token_num++));
    }
    token_reg |= (uint64_t)TOKEN_STOP << (4*(token_num++));

    dev->virt_regs->token_list_0 = (uint32_t)(token_reg & 0xffffffff);
    dev->virt_regs->token_list_1 = (uint32_t)((token_reg >> 32) & 0xffffffff);

    uint64_t wdata = 0;
    for (uint32_t i=0; i < len; i++) {
        wdata |= (uint64_t)buff[i] << (8*i);
    }

    dev->virt_regs->token_wdata_0 = (uint32_t)(wdata & 0xffffffff);
    dev->virt_regs->token_wdata_1 = (uint32_t)((wdata >> 32) & 0xffffffff);

    aml_i2c_start_xfer(dev);

    //while (dev->virt_regs->control & 0x4) ;;    // wait for idle
    return aml_i2c_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
}

zx_status_t aml_i2c_read(aml_i2c_dev_t *dev, uint8_t *buff, uint32_t len) {

    //temporary hack, only transactions that can fit in hw buffer
    ZX_DEBUG_ASSERT(len <= AML_I2C_MAX_TRANSFER);

    uint32_t token_num = 0;
    uint64_t token_reg = 0;

    token_reg |= (uint64_t)TOKEN_START << (4*(token_num++));
    token_reg |= (uint64_t)TOKEN_SLAVE_ADDR_RD << (4*(token_num++));

    for (uint32_t i=0; i < (len - 1); i++) {
        token_reg |= (uint64_t)TOKEN_DATA << (4*(token_num++));
    }
    token_reg |= (uint64_t)TOKEN_DATA_LAST << (4*(token_num++));
    token_reg |= (uint64_t)TOKEN_STOP << (4*(token_num++));

    dev->virt_regs->token_list_0 = (uint32_t)(token_reg & 0xffffffff);
    token_reg = token_reg >> 32;
    dev->virt_regs->token_list_1 = (uint32_t)(token_reg);

    //clear registers to prevent data leaking from last xfer
    dev->virt_regs->token_rdata_0 = 0;
    dev->virt_regs->token_rdata_1 = 0;

    aml_i2c_start_xfer(dev);

    zx_status_t status = aml_i2c_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
    if (status != ZX_OK) {
        return status;
    }

    //while (dev->virt_regs->control & 0x4) ;;    // wait for idle

    uint64_t rdata;
    rdata = dev->virt_regs->token_rdata_0;
    rdata |= (uint64_t)(dev->virt_regs->token_rdata_1) << 32;

    for (uint32_t i=0; i < sizeof(rdata); i++) {
        buff[i] = (uint8_t)((rdata >> (8*i) & 0xff));
    }

    return ZX_OK;
}

zx_status_t aml_i2c_connect(aml_i2c_connection_t **connection,
                             aml_i2c_dev_t *dev,
                             uint32_t i2c_addr,
                             uint32_t num_addr_bits) {

    if ((num_addr_bits != 7) && (num_addr_bits != 10))
        return ZX_ERR_INVALID_ARGS;

    aml_i2c_connection_t *conn;
    // Check if the i2c channel is already in use

    mtx_lock(&dev->conn_mutex);
    list_for_every_entry(&dev->connections, conn, aml_i2c_connection_t, node) {
        if (conn->slave_addr == i2c_addr) {
            mtx_unlock(&dev->conn_mutex);
            zxlogf(INFO, "i2c slave address already in use!\n");
            return ZX_ERR_INVALID_ARGS;
        }
    }

    conn = calloc(1, sizeof(aml_i2c_connection_t));
    if (!conn) {
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

void aml_i2c_release(aml_i2c_connection_t* conn) {
    mtx_lock(&conn->dev->conn_mutex);
    list_delete(&conn->node);
    mtx_unlock(&conn->dev->conn_mutex);
    free(conn);
}

/* create instance of aml_i2c_t and do basic initialization.  There will
be one of these instances for each of the soc i2c ports.
*/
zx_status_t aml_i2c_init(aml_i2c_dev_t **device, aml_i2c_port_t portnum) {

    aml_i2c_dev_desc_t *dev_desc = get_i2c_dev(portnum);
    if (!dev_desc) return ZX_ERR_INVALID_ARGS;

    *device = calloc(1, sizeof(aml_i2c_dev_t));
    if (!(*device)) {
        return ZX_ERR_NO_MEMORY;
    }

    //Initialize the connection list;
    list_initialize(&(*device)->connections);
    list_initialize(&(*device)->txn_list);
    list_initialize(&(*device)->free_txn_list);
    mtx_init(&(*device)->conn_mutex, mtx_plain);
    mtx_init(&(*device)->txn_mutex, mtx_plain);

    (*device)->txn_active =  COMPLETION_INIT;

    (*device)->timeout = ZX_SEC(1);

    zx_handle_t resource = get_root_resource();
    zx_status_t status;

    status = io_buffer_init_physical(&(*device)->regs_iobuff,
                                        dev_desc->base_phys,
                                        PAGE_SIZE, resource,
                                        ZX_CACHE_POLICY_UNCACHED_DEVICE);

    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_init: io_buffer_init_physical failed %d\n", status);
        goto init_fail;
    }

    (*device)->virt_regs = (aml_i2c_regs_t*)(io_buffer_virt(&(*device)->regs_iobuff));

    status = zx_interrupt_create(resource, dev_desc->irqnum, ZX_INTERRUPT_MODE_LEVEL_HIGH, &(*device)->irq);
    if (status != ZX_OK) {
        goto init_fail;
    }

    status = zx_event_create(0, &(*device)->event);
    if (status != ZX_OK) {
        goto init_fail;
    }


    thrd_t thrd;
    thrd_create_with_name(&thrd, aml_i2c_thread, *device, "i2c_thread");
    thrd_t irqthrd;
    thrd_create_with_name(&irqthrd, aml_i2c_irq_thread, *device, "i2c_irq_thread");

    return ZX_OK;

init_fail:
    if (*device) {
        io_buffer_release(&(*device)->regs_iobuff);
        if ((*device)->event != ZX_HANDLE_INVALID)
            zx_handle_close((*device)->event);
        if ((*device)->irq != ZX_HANDLE_INVALID)
            zx_handle_close((*device)->irq);
        free(*device);
     };
    return status;
}
