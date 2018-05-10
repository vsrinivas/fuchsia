// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <ddk/protocol/scpi.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include "aml-scpi.h"
#include "aml-mailbox.h"

static zx_status_t aml_scpi_get_mailbox(uint32_t cmd,
                                    uint32_t *mailbox) {
    if (!mailbox || !VALID_CMD(cmd)) {
        return ZX_ERR_INVALID_ARGS;
    }

    for (uint32_t i=0; i<countof(aml_low_priority_cmds); i++) {
        if (cmd == aml_low_priority_cmds[i]) {
            *mailbox = AP_NS_LOW_PRIORITY_MAILBOX;
            return ZX_OK;
        }
    }

    for (uint32_t i=0; i<countof(aml_high_priority_cmds); i++) {
        if (cmd == aml_low_priority_cmds[i]) {
            *mailbox = AP_NS_HIGH_PRIORITY_MAILBOX;
            return ZX_OK;
        }
    }

    for (uint32_t i=0; i<countof(aml_secure_cmds); i++) {
        if (cmd == aml_low_priority_cmds[i]) {
            *mailbox = AP_SECURE_MAILBOX;
            return ZX_OK;
        }
    }
    *mailbox = INVALID_MAILBOX;
    return ZX_ERR_NOT_FOUND;
}

zx_status_t aml_scpi_get_sensor_value(void* ctx, uint32_t sensor_id,
                                      uint32_t* sensor_value) {
    aml_scpi_t* scpi = ctx;
    struct {
        uint32_t status;
        uint16_t sensor_value;
    } __PACKED aml_sensor_val;

    mailbox_data_buf_t mdata;
    mdata.cmd             = PACK_SCPI_CMD(SCPI_CMD_SENSOR_VALUE, SCPI_CL_THERMAL, 0);
    mdata.tx_buf          = (void*)&sensor_id;
    mdata.tx_size         = sizeof(sensor_id);

    mailbox_channel_t channel;
    zx_status_t status = aml_scpi_get_mailbox(SCPI_CMD_SENSOR_VALUE, &channel.mailbox);
    if (status != ZX_OK) {
        SCPI_ERROR("aml_scpi_get_mailbox failed - error status %d\n", status);
        return status;
    }

    channel.rx_size = sizeof(aml_sensor_val);
    channel.rx_buf  = (void*)&aml_sensor_val;

    status = mailbox_send_cmd(&scpi->mailbox, &channel, &mdata);
    if (status != ZX_OK || aml_sensor_val.status != 0) {
        SCPI_ERROR("mailbox_send_cmd failed - error status %d\n", status);
        return status;
    }

    *sensor_value = aml_sensor_val.sensor_value;
    return ZX_OK;
}

zx_status_t aml_scpi_get_sensor(void* ctx, const char* name,
                                uint32_t* sensor_value) {
    aml_scpi_t* scpi = ctx;
    struct {
        uint32_t status;
        uint16_t num_sensors;
    } __PACKED aml_sensor_cap;
    struct {
        uint32_t status;
        uint32_t sensor;
        char sensor_name[20];
    } __PACKED aml_sensor_info;

    if (sensor_value == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    mailbox_channel_t channel;
    zx_status_t status = aml_scpi_get_mailbox(SCPI_CMD_SENSOR_CAPABILITIES, &channel.mailbox);
    if (status != ZX_OK) {
        SCPI_ERROR("aml_scpi_get_mailbox failed \n");
        return status;
    }

    channel.rx_size = sizeof(aml_sensor_cap);
    channel.rx_buf  = (void *)&aml_sensor_cap;

    mailbox_data_buf_t mdata;
    mdata.tx_size   = 0;
    mdata.cmd       = PACK_SCPI_CMD(SCPI_CMD_SENSOR_CAPABILITIES, SCPI_CL_THERMAL, 0);

    // First let's find how many sensors are there on the board
    status = mailbox_send_cmd(&scpi->mailbox, &channel, &mdata);
    if (status != ZX_OK || aml_sensor_cap.status != 0) {
        SCPI_ERROR("mailbox_send_cmd failed\n");
        return status;
    }

    status = aml_scpi_get_mailbox(SCPI_CMD_SENSOR_INFO, &channel.mailbox);
    if (status != ZX_OK) {
        SCPI_ERROR("aml_scpi_get_mailbox failed \n");
        return status;
    }
    channel.rx_size = sizeof(aml_sensor_info);
    channel.rx_buf  = (void *)&aml_sensor_info;

    mdata.cmd          = PACK_SCPI_CMD(SCPI_CMD_SENSOR_INFO, SCPI_CL_THERMAL, 0);

    // Loop through all the sensors
    for (uint16_t sensor_id=0; sensor_id<aml_sensor_cap.num_sensors; sensor_id++) {
        mdata.tx_buf    = (void*)&sensor_id;
        mdata.tx_size   = sizeof(sensor_id);
        status = mailbox_send_cmd(&scpi->mailbox, &channel, &mdata);
        if (status != ZX_OK || aml_sensor_info.status != 0) {
            SCPI_ERROR("mailbox_send_cmd failed\n");
            return status;
        }
        if (!strncmp(name, aml_sensor_info.sensor_name, sizeof(aml_sensor_info.sensor_name))) {
            *sensor_value = aml_sensor_info.sensor;
            break;
        }
    }
    return ZX_OK;
}

static void aml_scpi_release(void* ctx) {
    aml_scpi_t* scpi = ctx;
    free(scpi);
}

static zx_protocol_device_t aml_scpi_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_scpi_release,
};

static scpi_protocol_ops_t scpi_ops = {
    .get_sensor         = aml_scpi_get_sensor,
    .get_sensor_value   = aml_scpi_get_sensor_value,
};

static zx_status_t aml_scpi_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    aml_scpi_t *scpi = calloc(1, sizeof(aml_scpi_t));
    if (!scpi) {
        return ZX_ERR_NO_MEMORY;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &scpi->pdev);
    if (status !=  ZX_OK) {
        SCPI_ERROR("Could not get parent protocol\n");
        goto fail;
    }
    status = device_get_protocol(parent, ZX_PROTOCOL_MAILBOX, &scpi->mailbox);
    if (status != ZX_OK) {
        SCPI_ERROR("Could not get Mailbox protocol\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-scpi",
        .ctx = scpi,
        .ops = &aml_scpi_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    scpi->scpi.ops = &scpi_ops;
    scpi->scpi.ctx = scpi;

    platform_bus_protocol_t pbus;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) != ZX_OK) {
        SCPI_ERROR("ZX_PROTOCOL_PLATFORM_BUS not available %d \n",status);
        goto fail;
    }

    pbus_set_protocol(&pbus, ZX_PROTOCOL_SCPI, &scpi->scpi);
    return ZX_OK;
fail:
    aml_scpi_release(scpi);
    return ZX_OK;
}

static zx_driver_ops_t aml_scpi_driver_ops = {
    .version    = DRIVER_OPS_VERSION,
    .bind       = aml_scpi_bind,
};

ZIRCON_DRIVER_BEGIN(aml_scpi, aml_scpi_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SCPI),
ZIRCON_DRIVER_END(aml_scpi)
