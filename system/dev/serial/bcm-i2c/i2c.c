// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/platform-device.h>

#include <magenta/syscalls.h>
#include <magenta/threads.h>
#include <magenta/device/i2c.h>

#include <bcm/gpio.h>
#include <bcm/bcm28xx.h>
#include <bcm/ioctl.h>

#include "i2c.h"

#define BCM_MAX_SEGMENTS 20
#define BCM_FIFO_DEADLINE_MS 100

#define GPIO_MMIO   0

typedef struct {
    mx_device_t*                parent;
    platform_device_protocol_t  pdev_proto;
    bcm_i2c_regs_t*             control_regs;
    uint32_t                    dev_id;
} bcm_i2c_t;

/* TODO - improve fifo read/write to be interrupt driven and capable of handling
            multiple transactions in a buffer at once.

        Right now we limit the transaction size to <= 16 bytes (fifo size)
        and wait for transaction to complete before exiting.  This allows
        us to keep transactions easily framed.  This driver is being written
        to only support the PCM5121 codec at this point, so these limitations
        are a reasonable tradeoff at this time.

*/
static mx_status_t bcm_write_fifo(bcm_i2c_t* ctx, uint8_t* data, uint32_t len){

    if (len > BCM_BSC_FIFO_SIZE)
        return MX_ERR_INVALID_ARGS;

    ctx->control_regs->dlen = (uint32_t)len;
    ctx->control_regs->control = BCM_BSC_CONTROL_ENABLE | BCM_BSC_CONTROL_START;
    for (uint32_t i = 0; i < len; i++)
        ctx->control_regs->fifo = data[i];

    mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_MSEC(BCM_FIFO_DEADLINE_MS);

    while( !(ctx->control_regs->status & BCM_BSC_STATUS_DONE) ) {
        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            printf("FIFO write timed out\n");
            return MX_ERR_TIMED_OUT;
        }
    }

    if (ctx->control_regs->status & BCM_BSC_STATUS_ERR) {
        ctx->control_regs->status |= (BCM_BSC_STATUS_ERR | BCM_BSC_STATUS_DONE);
        return MX_ERR_TIMED_OUT;
    }

    ctx->control_regs->status |= BCM_BSC_STATUS_DONE;   //clear the done status

    return MX_OK;
}

static mx_status_t bcm_read_fifo(bcm_i2c_t* ctx, uint8_t* data, uint32_t len){

    if (len > BCM_BSC_FIFO_SIZE)
        return MX_ERR_INVALID_ARGS;

    ctx->control_regs->dlen = (uint32_t)len;
    ctx->control_regs->control = BCM_BSC_CONTROL_ENABLE | BCM_BSC_CONTROL_START |
                                 BCM_BSC_CONTROL_READ;

    mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_MSEC(BCM_FIFO_DEADLINE_MS);

    while( !(ctx->control_regs->status & BCM_BSC_STATUS_DONE) ){
        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline){
            printf("FIFO read timed out\n");
            return MX_ERR_TIMED_OUT;
        }
    }
    if (ctx->control_regs->status & BCM_BSC_STATUS_ERR) {
        for (uint32_t i = 0; i < len; i++)
            data[i] = (uint8_t)0;
        ctx->control_regs->status |= (BCM_BSC_STATUS_ERR | BCM_BSC_STATUS_DONE);
        return MX_ERR_TIMED_OUT;
    }

    ctx->control_regs->status |= BCM_BSC_STATUS_DONE;   //clear the done status


    for (uint32_t i = 0; i < len; i++)
        data[i] = (uint8_t)ctx->control_regs->fifo;
    return MX_OK;
}

static mx_status_t bcm_i2c_read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {

    bcm_i2c_t* i2c_ctx = ctx;
    mx_status_t status = bcm_read_fifo(i2c_ctx,(uint8_t*)buf,(uint32_t)count);
    if (status == MX_OK) {
        *actual = count;
    }
    return status;
}

static mx_status_t bcm_i2c_write(void* ctx, const void* buf, size_t count, mx_off_t off, size_t* actual) {

    bcm_i2c_t* i2c_ctx = ctx;

    mx_status_t status = bcm_write_fifo(i2c_ctx,(uint8_t*)buf,(uint32_t)count);

    if (status == MX_OK) {
        *actual = count;
        return MX_OK;
    } else {
        return -1;
    }
}

static mx_status_t bcm_i2c_set_slave_addr(bcm_i2c_t* ctx, uint16_t address){

    ctx->control_regs->slave_addr = (uint32_t)address;

    return MX_OK;
}

static mx_status_t bcm_i2c_slave_transfer(bcm_i2c_t* ctx, const void* in_buf, size_t in_len,
    void* out_buf, size_t out_len) {

    mx_status_t status = MX_OK;
    uint32_t num_segments=0;


    i2c_slave_ioctl_segment_t* segments = (i2c_slave_ioctl_segment_t*)in_buf;

    // figure out how many segments are in this ioctl trnasfer
    while ( (!(segments[num_segments].type == I2C_SEGMENT_TYPE_END)) & (num_segments < BCM_MAX_SEGMENTS)) {
        num_segments++;
    }
    if (num_segments >= BCM_MAX_SEGMENTS)
        return MX_ERR_INVALID_ARGS;

    uint8_t* data =(uint8_t*)&segments[num_segments + 1];   // +1 to skip the end marker
    uint32_t num_writes = 0;
    uint32_t num_reads = 0;
    for (uint32_t i = 0; i < num_segments; i++ ){
        if (segments[i].type == I2C_SEGMENT_TYPE_WRITE) {
            status = bcm_write_fifo(ctx, &data[num_writes], segments[i].len);
            if (status != MX_OK)
                return status;
            num_writes += segments[i].len;
        } else if (segments[i].type == I2C_SEGMENT_TYPE_READ) {
            status = bcm_read_fifo(ctx, &((uint8_t*)out_buf)[num_reads], segments[i].len);
            if (status != MX_OK)
                return status;
            num_reads += segments[i].len;
        }
    }

    return MX_OK;
}

static mx_status_t bcm_i2c_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    bcm_i2c_t* i2c_ctx = ctx;
    int ret;

    switch (op) {
    case IOCTL_I2C_BUS_ADD_SLAVE: {

        const i2c_ioctl_add_slave_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return MX_ERR_INVALID_ARGS;

        if (args->chip_address_width == 7) {
            ret =  bcm_i2c_set_slave_addr(i2c_ctx,args->chip_address);
        } else {
            return MX_ERR_INVALID_ARGS;
        }
        break;
    }
    case IOCTL_I2C_BUS_REMOVE_SLAVE: {
        ret = MX_OK;
        break;
    }
    case IOCTL_I2C_SLAVE_TRANSFER: {
        ret = bcm_i2c_slave_transfer(i2c_ctx, in_buf, in_len, out_buf, out_len);
        break;
    }
    case IOCTL_I2C_BUS_SET_FREQUENCY: {
        ret = MX_OK;
        break;
    }
    default:
        return MX_ERR_INVALID_ARGS;
    }

    if (ret == MX_OK && out_len > 0 && out_actual) {
        *out_actual = out_len;
    }
    return ret;
}

static mx_protocol_device_t i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = bcm_i2c_read,
    .write = bcm_i2c_write,
    .ioctl = bcm_i2c_ioctl,
};

static int i2c_bootstrap_thread(void *arg) {

    assert(arg);

    bcm_i2c_t* i2c_ctx = (bcm_i2c_t*)arg;

    size_t mmio_size;
    mx_handle_t mmio_handle = MX_HANDLE_INVALID;
    mx_status_t status = pdev_map_mmio(&i2c_ctx->pdev_proto, i2c_ctx->dev_id,
                                              MX_CACHE_POLICY_UNCACHED_DEVICE,
                                              (void **)&i2c_ctx->control_regs, &mmio_size,
                                              &mmio_handle);
    if (status != MX_OK)
        goto i2c_err;

    i2c_ctx->control_regs->control = BCM_BSC_CONTROL_ENABLE | BCM_BSC_CONTROL_FIFO_CLEAR;

    i2c_ctx->control_regs->clk_div = BCM_BSC_CLK_DIV_100K;

    char id[5];
    snprintf(id,sizeof(id),"i2c%u",i2c_ctx->dev_id);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = id,
        .ctx = i2c_ctx,
        .ops = &i2c_device_proto,
    };

    status = device_add(i2c_ctx->parent, &args, NULL);

    if (status == MX_OK) return 0;

i2c_err:
    free(i2c_ctx);

    return -1;
}

static mx_status_t bootstrap_i2c(mx_device_t* parent, platform_device_protocol_t* pdev_proto,
                                 uint32_t dev_id) {

    bcm_i2c_t* i2c_ctx = calloc(1, sizeof(*i2c_ctx));
    if (!i2c_ctx)
        return MX_ERR_NO_MEMORY;

    i2c_ctx->parent     = parent;
    i2c_ctx->dev_id     = dev_id;
    memcpy(&i2c_ctx->pdev_proto, pdev_proto, sizeof(i2c_ctx->pdev_proto));

    char tid[30];
    snprintf(tid,sizeof(tid),"i2c%d_bootstrap_thread",dev_id);

    thrd_t bootstrap_thrd;
    int thrd_rc = thrd_create_with_name(&bootstrap_thrd,
                                        i2c_bootstrap_thread, i2c_ctx, tid);
    if (thrd_rc != thrd_success) {
        free(i2c_ctx);
        return thrd_status_to_mx_status(thrd_rc);
    }
    thrd_detach(bootstrap_thrd);
    return MX_OK;
}


static mx_status_t i2c_bind(void* ctx, mx_device_t* parent, void** cookie) {
    platform_device_protocol_t pdev;

    mx_status_t ret = device_get_protocol(parent, MX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (ret != MX_OK) {
        printf("i2c_bind can't find MX_PROTOCOL_PLATFORM_DEV\n");
        return ret;
    }

    bcm_gpio_ctrl_t* gpio_regs;
    // Carve out some address space for the device -- it's memory mapped.
    size_t mmio_size;
    mx_handle_t mmio_handle = MX_HANDLE_INVALID;
    mx_status_t status = pdev_map_mmio(&pdev, GPIO_MMIO, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                            (void **)&gpio_regs, &mmio_size, &mmio_handle);
    if (status != MX_OK) {
        printf("i2c_bind: pdev_map_mmio failed: %d\n", status);
        return status;
    }

    /* ALT Function 0 is I2C for these pins */
    set_gpio_function(gpio_regs, BCM_SDA1_PIN, FSEL_ALT0);
    set_gpio_function(gpio_regs, BCM_SCL1_PIN, FSEL_ALT0);

    set_gpio_function(gpio_regs, BCM_SDA0_PIN, FSEL_ALT0);
    set_gpio_function(gpio_regs, BCM_SCL0_PIN, FSEL_ALT0);


    status = bootstrap_i2c(parent, &pdev, 0);
    if (status != MX_OK) {
        ret = status;
        printf("Failed to initialize i2c0\n");
    }

    status = bootstrap_i2c(parent, &pdev, 1);
    if (status != MX_OK) {
        ret = status;
        printf("Failed to initialize i2c1\n");
    }

    return ret;
}

static mx_driver_ops_t bcm_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = i2c_bind,
};

MAGENTA_DRIVER_BEGIN(bcm_i2c, bcm_i2c_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_BROADCOMM_I2C),
MAGENTA_DRIVER_END(bcm_i2c)
