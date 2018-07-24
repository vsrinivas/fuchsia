// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/reg.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#define I2C_ERROR_SIGNAL ZX_USER_SIGNAL_0
#define I2C_TXN_COMPLETE_SIGNAL ZX_USER_SIGNAL_1

#define AML_I2C_CONTROL_REG_START      (uint32_t)(1 << 0)
#define AML_I2C_CONTROL_REG_ACK_IGNORE (uint32_t)(1 << 1)
#define AML_I2C_CONTROL_REG_STATUS     (uint32_t)(1 << 2)
#define AML_I2C_CONTROL_REG_ERR        (uint32_t)(1 << 3)

#define AML_I2C_MAX_TRANSFER 256

typedef volatile struct {
    uint32_t    control;
    uint32_t    slave_addr;
    uint32_t    token_list_0;
    uint32_t    token_list_1;
    uint32_t    token_wdata_0;
    uint32_t    token_wdata_1;
    uint32_t    token_rdata_0;
    uint32_t    token_rdata_1;
} __PACKED aml_i2c_regs_t;

typedef enum {
    TOKEN_END,
    TOKEN_START,
    TOKEN_SLAVE_ADDR_WR,
    TOKEN_SLAVE_ADDR_RD,
    TOKEN_DATA,
    TOKEN_DATA_LAST,
    TOKEN_STOP
} aml_i2c_token_t;

typedef struct {
    zx_handle_t         irq;
    zx_handle_t         event;
    io_buffer_t         regs_iobuff;
    aml_i2c_regs_t*     virt_regs;
    zx_duration_t       timeout;
} aml_i2c_dev_t;

typedef struct {
    platform_device_protocol_t pdev;
    i2c_impl_protocol_t i2c;
    zx_device_t* zxdev;
    aml_i2c_dev_t* i2c_devs;
    size_t dev_count;
} aml_i2c_t;

static zx_status_t aml_i2c_set_slave_addr(aml_i2c_dev_t *dev, uint16_t addr) {

    addr &= 0x7f;
    uint32_t reg = dev->virt_regs->slave_addr;
    reg = reg & ~0xff;
    reg = reg | ((addr << 1) & 0xff);
    dev->virt_regs->slave_addr = reg;

    return ZX_OK;
}

static int aml_i2c_irq_thread(void *arg) {

    aml_i2c_dev_t *dev = arg;
    zx_status_t status;

    while (1) {
        status = zx_interrupt_wait(dev->irq, NULL);
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
    }
    return ZX_OK;
}

static zx_status_t aml_i2c_dumpstate(aml_i2c_dev_t *dev) {

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

static zx_status_t aml_i2c_start_xfer(aml_i2c_dev_t *dev) {
    //First have to clear the start bit before setting (RTFM)
    dev->virt_regs->control &= ~AML_I2C_CONTROL_REG_START;
    dev->virt_regs->control |= AML_I2C_CONTROL_REG_START;
    return ZX_OK;
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

static zx_status_t aml_i2c_write(aml_i2c_dev_t *dev, const uint8_t *buff, uint32_t len) {
    ZX_DEBUG_ASSERT(len <= AML_I2C_MAX_TRANSFER);
    uint32_t token_num = 0;
    uint64_t token_reg = 0;

    token_reg |= (uint64_t)TOKEN_START << (4*(token_num++));
    token_reg |= (uint64_t)TOKEN_SLAVE_ADDR_WR << (4*(token_num++));

    while (len > 0) {
        bool is_last_iter = len <= 8;
        uint32_t tx_size = is_last_iter ? len : 8;
        for (uint32_t i=0; i < tx_size; i++) {
            token_reg |= (uint64_t)TOKEN_DATA << (4*(token_num++));
        }

        if (is_last_iter) {
            token_reg |= (uint64_t)TOKEN_STOP << (4*(token_num++));
        }

        dev->virt_regs->token_list_0 = (uint32_t)(token_reg & 0xffffffff);
        dev->virt_regs->token_list_1 =
            (uint32_t)((token_reg >> 32) & 0xffffffff);

        uint64_t wdata = 0;
        for (uint32_t i=0; i < tx_size; i++) {
            wdata |= (uint64_t)buff[i] << (8*i);
        }

        dev->virt_regs->token_wdata_0 = (uint32_t)(wdata & 0xffffffff);
        dev->virt_regs->token_wdata_1 = (uint32_t)((wdata >> 32) & 0xffffffff);

        aml_i2c_start_xfer(dev);
        //while (dev->virt_regs->control & 0x4) ;;    // wait for idle
        zx_status_t status = aml_i2c_wait_event(dev, I2C_TXN_COMPLETE_SIGNAL);
        if (status != ZX_OK) {
            return status;
        }

        len -= tx_size;
        buff += tx_size;
        token_num = 0;
        token_reg = 0;
    }

    return ZX_OK;
}

static zx_status_t aml_i2c_read(aml_i2c_dev_t *dev, uint8_t *buff, uint32_t len) {

    ZX_DEBUG_ASSERT(len <= AML_I2C_MAX_TRANSFER);
    uint32_t token_num = 0;
    uint64_t token_reg = 0;

    token_reg |= (uint64_t)TOKEN_START << (4*(token_num++));
    token_reg |= (uint64_t)TOKEN_SLAVE_ADDR_RD << (4*(token_num++));

    while (len > 0) {
        bool is_last_iter = len <= 8;
        uint32_t rx_size = is_last_iter ? len : 8;

        for (uint32_t i=0; i < (rx_size - 1); i++) {
            token_reg |= (uint64_t)TOKEN_DATA << (4*(token_num++));
        }
        if (is_last_iter) {
            token_reg |= (uint64_t)TOKEN_DATA_LAST << (4*(token_num++));
            token_reg |= (uint64_t)TOKEN_STOP << (4*(token_num++));
        } else {
            token_reg |= (uint64_t)TOKEN_DATA << (4*(token_num++));
        }

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

        len -= rx_size;
        buff += rx_size;
        token_num = 0;
        token_reg = 0;
    }

    return ZX_OK;
}

/* create instance of aml_i2c_t and do basic initialization.  There will
be one of these instances for each of the soc i2c ports.
*/
static zx_status_t aml_i2c_dev_init(aml_i2c_t* i2c, unsigned index) {
    aml_i2c_dev_t* device = &i2c->i2c_devs[index];

    device->timeout = ZX_SEC(1);

    zx_status_t status;

    status = pdev_map_mmio_buffer(&i2c->pdev, index, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &device->regs_iobuff);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_dev_init: pdev_map_mmio_buffer failed %d\n", status);
        goto init_fail;
    }

    device->virt_regs = (aml_i2c_regs_t*)io_buffer_virt(&device->regs_iobuff);

    status = pdev_map_interrupt(&i2c->pdev, index, &device->irq);
    if (status != ZX_OK) {
        goto init_fail;
    }

    status = zx_event_create(0, &device->event);
    if (status != ZX_OK) {
        goto init_fail;
    }

    thrd_t irqthrd;
    thrd_create_with_name(&irqthrd, aml_i2c_irq_thread, device, "i2c_irq_thread");

    return ZX_OK;

init_fail:
    if (device) {
        io_buffer_release(&device->regs_iobuff);
        zx_handle_close(device->event);
        zx_handle_close(device->irq);
        free(device);
     }
    return status;
}

static uint32_t aml_i2c_get_bus_count(void* ctx) {
    aml_i2c_t* i2c = ctx;

    return i2c->dev_count;
}

static zx_status_t aml_i2c_get_max_transfer_size(void* ctx, uint32_t bus_id, size_t* out_size) {
    *out_size = AML_I2C_MAX_TRANSFER;
    return ZX_OK;
}

static zx_status_t aml_i2c_set_bitrate(void* ctx, uint32_t bus_id, uint32_t bitrate) {
    // TODO(hollande,voydanoff) implement this
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t aml_i2c_transact(void* ctx, uint32_t bus_id, uint16_t address,
                                    const void* write_buf, size_t write_length,
                                    void* read_buf, size_t read_length) {
    if (!read_length && !write_length) {
        return ZX_ERR_INVALID_ARGS;
    }
    aml_i2c_t* i2c = ctx;
    if (bus_id >= i2c->dev_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    aml_i2c_dev_t *dev = &i2c->i2c_devs[bus_id];

    zx_status_t status = aml_i2c_set_slave_addr(dev, address);
    if (status != ZX_OK) {
        return status;
    }

    if (write_length) {
        status = aml_i2c_write(dev, write_buf, write_length);
        if (status != ZX_OK) {
            return status;
        }
    }
    if (read_length) {
        status = aml_i2c_read(dev, read_buf, read_length);
        if (status != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

static i2c_impl_ops_t i2c_ops = {
    .get_bus_count = aml_i2c_get_bus_count,
    .get_max_transfer_size = aml_i2c_get_max_transfer_size,
    .set_bitrate = aml_i2c_set_bitrate,
    .transact = aml_i2c_transact,
};

static void aml_i2c_release(void* ctx) {
    aml_i2c_t* i2c = ctx;
    for (unsigned i = 0; i < i2c->dev_count; i++) {
        aml_i2c_dev_t* device = &i2c->i2c_devs[i];
        io_buffer_release(&device->regs_iobuff);
        zx_handle_close(device->event);
        zx_handle_close(device->irq);
    }
    free(i2c->i2c_devs);
    free(i2c);
}

static zx_protocol_device_t i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_i2c_release,
};

static zx_status_t aml_i2c_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    aml_i2c_t* i2c = calloc(1, sizeof(aml_i2c_t));
    if (!i2c) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &i2c->pdev)) != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_bind: ZX_PROTOCOL_PLATFORM_DEV not available\n");
        goto fail;
    }

    platform_bus_protocol_t pbus;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_bind: ZX_PROTOCOL_PLATFORM_BUS not available\n");
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&i2c->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_bind: pdev_get_device_info failed\n");
        goto fail;
    }

    if (info.mmio_count != info.irq_count) {
        zxlogf(ERROR, "aml_i2c_bind: mmio_count %u does not matchirq_count %u\n",
               info.mmio_count, info.irq_count);
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }
    i2c->i2c_devs = calloc(info.mmio_count, sizeof(aml_i2c_dev_t));
    if (!i2c->i2c_devs) {
        goto fail;
    }
    i2c->dev_count = info.mmio_count;

    for (unsigned i = 0; i < i2c->dev_count; i++) {
        zx_status_t status = aml_i2c_dev_init(i2c, i);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_i2c_bind: aml_i2c_dev_init failed: %d\n", status);
            goto fail;
        }
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-i2c",
        .ctx = i2c,
        .ops = &i2c_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &i2c->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_bind: device_add failed\n");
        goto fail;
    }

    i2c->i2c.ops = &i2c_ops;
    i2c->i2c.ctx = i2c;
    pbus_set_protocol(&pbus, ZX_PROTOCOL_I2C_IMPL, &i2c->i2c);

    return ZX_OK;

fail:
    aml_i2c_release(i2c);
    return status;
}

static zx_driver_ops_t aml_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_i2c_bind,
};

ZIRCON_DRIVER_BEGIN(aml_i2c, aml_i2c_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_I2C),
ZIRCON_DRIVER_END(aml_i2c)
