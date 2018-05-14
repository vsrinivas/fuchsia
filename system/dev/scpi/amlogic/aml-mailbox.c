// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include "aml-mailbox.h"
#include "aml-mailbox-hw.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/mailbox.h>

#define READ32_MAILBOX_PL_REG(offset)         readl(io_buffer_virt(&mailbox->mmio_mailbox_payload)\
                                                    + (offset)*4)
#define WRITE32_MAILBOX_PL_REG(offset, value) writel(value, \
                                                    io_buffer_virt(&mailbox->mmio_mailbox_payload)\
                                                    + (offset)*4)
#define READ32_MAILBOX_REG(offset)            readl(io_buffer_virt(&mailbox->mmio_mailbox) \
                                                    + (offset)*4)
#define WRITE32_MAILBOX_REG(offset, value)    writel(value, io_buffer_virt(&mailbox->mmio_mailbox)\
                                                     + (offset)*4)

static int aml_get_rx_mailbox(uint32_t tx_mailbox) {
    switch(tx_mailbox) {
        case AP_SECURE_MAILBOX:
            return SCP_SECURE_MAILBOX;
        case AP_NS_LOW_PRIORITY_MAILBOX:
            return SCP_NS_LOW_PRIORITY_MAILBOX;
        case AP_NS_HIGH_PRIORITY_MAILBOX:
            return SCP_NS_HIGH_PRIORITY_MAILBOX;
        default:
            return INVALID_MAILBOX;
    }
}

static zx_status_t aml_mailbox_send_cmd(void *ctx,
                                 mailbox_channel_t *channel,
                                 mailbox_data_buf_t* mdata) {
    aml_mailbox_t* mailbox = ctx;
    int rx_mailbox_id;
    if (!channel || !mdata) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (INVALID_MAILBOX == (rx_mailbox_id =
        aml_get_rx_mailbox(channel->mailbox))) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&mailbox->mailbox_chan_lock[channel->mailbox]);
    aml_mailbox_block_t *rx_mailbox = &vim2_mailbox_block[rx_mailbox_id];
    aml_mailbox_block_t *tx_mailbox = &vim2_mailbox_block[channel->mailbox];

    if (mdata->tx_size != 0) {
        uint32_t num = GET_NUM_WORDS(mdata->tx_size);
        uint32_t *tx_payload = (uint32_t*)(mdata->tx_buf);
        for (uint32_t i =0; i<num; i++) {
            // AP writes parameters to Payload
            WRITE32_MAILBOX_PL_REG(tx_mailbox->payload_offset + i, tx_payload[i]);
        }
    }

    // AP writes command to AP Mailbox
    WRITE32_MAILBOX_REG(tx_mailbox->set_offset, mdata->cmd);

    zx_status_t status = zx_interrupt_wait(mailbox->inth[rx_mailbox_id], NULL);
    if (status != ZX_OK) {
        MAILBOX_ERROR("zx_interrupt_wait failed\n");
        mtx_unlock(&mailbox->mailbox_chan_lock[channel->mailbox]);
        return status;
    }

    // AP reads the Payload to get requested information
    if (channel->rx_size != 0) {
        uint32_t num = GET_NUM_WORDS(channel->rx_size);
        uint32_t *rx_payload = (uint32_t*)(channel->rx_buf);
        for (uint32_t i=0; i<num; i++) {
            rx_payload[i] = READ32_MAILBOX_PL_REG(rx_mailbox->payload_offset + i);
        }
    }

    // AP writes to the Mailbox CLR register
    WRITE32_MAILBOX_REG(rx_mailbox->clr_offset, 1);

    mtx_unlock(&mailbox->mailbox_chan_lock[channel->mailbox]);
    return ZX_OK;
}


static void aml_mailbox_release(void* ctx) {
    aml_mailbox_t* mailbox = ctx;
    io_buffer_release(&mailbox->mmio_mailbox);
    io_buffer_release(&mailbox->mmio_mailbox_payload);
    for (uint32_t i=0; i<NUM_MAILBOXES; i++) {
        zx_interrupt_destroy(mailbox->inth[i]);
        zx_handle_close(mailbox->inth[i]);
    }
    free(mailbox);
}

static zx_protocol_device_t aml_mailbox_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_mailbox_release,
};

static mailbox_protocol_ops_t mailbox_ops = {
    .send_cmd = aml_mailbox_send_cmd,
};

static zx_status_t aml_mailbox_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    aml_mailbox_t *mailbox = calloc(1, sizeof(aml_mailbox_t));
    if (!mailbox) {
        return ZX_ERR_NO_MEMORY;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &mailbox->pdev);
    if (status !=  ZX_OK) {
        MAILBOX_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&mailbox->pdev, &info);
    if (status != ZX_OK) {
        MAILBOX_ERROR("pdev_get_device_info failed\n");
        goto fail;
    }

    // Map all MMIOs
    status = pdev_map_mmio_buffer(&mailbox->pdev, MMIO_MAILBOX,
                                  ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &mailbox->mmio_mailbox);
    if (status != ZX_OK) {
        MAILBOX_ERROR("Could not map mailbox MMIO_MAILBOX %d\n",status);
        goto fail;
    }

    status = pdev_map_mmio_buffer(&mailbox->pdev, MMIO_MAILBOX_PAYLOAD,
                                  ZX_CACHE_POLICY_UNCACHED_DEVICE,
        &mailbox->mmio_mailbox_payload);
    if (status != ZX_OK) {
        MAILBOX_ERROR("Could not map mailbox MMIO_MAILBOX_PAYLOAD %d\n",status);
        goto fail;
    }

    for (uint32_t i=0; i<NUM_MAILBOXES; i++) {
        status = pdev_map_interrupt(&mailbox->pdev, i,
                                    &mailbox->inth[i]);
        if (status != ZX_OK) {
            MAILBOX_ERROR("pdev_map_interrupt failed %d\n", status);
            goto fail;
        }

        mtx_init(&mailbox->mailbox_chan_lock[i], mtx_plain);
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-mailbox",
        .ctx = mailbox,
        .ops = &aml_mailbox_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    mailbox->mailbox.ops = &mailbox_ops;
    mailbox->mailbox.ctx = mailbox;

    platform_bus_protocol_t pbus;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) != ZX_OK) {
        MAILBOX_ERROR("ZX_PROTOCOL_PLATFORM_BUS not available %d \n",status);
        goto fail;
    }

    pbus_set_protocol(&pbus, ZX_PROTOCOL_MAILBOX, &mailbox->mailbox);
    return ZX_OK;
fail:
    aml_mailbox_release(mailbox);
    return ZX_OK;
}

static zx_driver_ops_t aml_mailbox_driver_ops = {
    .version    = DRIVER_OPS_VERSION,
    .bind       = aml_mailbox_bind,
};

ZIRCON_DRIVER_BEGIN(aml_mailbox, aml_mailbox_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_MAILBOX),
ZIRCON_DRIVER_END(aml_mailbox)

