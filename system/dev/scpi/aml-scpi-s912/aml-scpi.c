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

static scpi_opp_t *scpi_opp[MAX_DVFS_DOMAINS];

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
        if (cmd == aml_high_priority_cmds[i]) {
            *mailbox = AP_NS_HIGH_PRIORITY_MAILBOX;
            return ZX_OK;
        }
    }

    for (uint32_t i=0; i<countof(aml_secure_cmds); i++) {
        if (cmd == aml_secure_cmds[i]) {
            *mailbox = AP_SECURE_MAILBOX;
            return ZX_OK;
        }
    }
    *mailbox = INVALID_MAILBOX;
    return ZX_ERR_NOT_FOUND;
}

static zx_status_t aml_scpi_execute_cmd(aml_scpi_t* scpi,
                                        void* rx_buf, size_t rx_size,
                                        void* tx_buf, size_t tx_size,
                                        uint32_t cmd, uint32_t client_id) {
    uint32_t mailbox_status = 0;
    mailbox_data_buf_t mdata;
    mdata.cmd             = PACK_SCPI_CMD(cmd, client_id, 0);
    mdata.tx_buf          = tx_buf;
    mdata.tx_size         = tx_size;

    mailbox_channel_t channel;
    zx_status_t status = aml_scpi_get_mailbox(cmd, &channel.mailbox);
    if (status != ZX_OK) {
        SCPI_ERROR("aml_scpi_get_mailbox failed - error status %d\n", status);
        return status;
    }

    channel.rx_buf  = rx_buf;
    channel.rx_size = rx_size;

    status = mailbox_send_cmd(&scpi->mailbox, &channel, &mdata);
    if (rx_buf) {
        mailbox_status = *(uint32_t*)(rx_buf);
    }
    if (status != ZX_OK || mailbox_status != 0) {
        SCPI_ERROR("mailbox_send_cmd failed - error status %d\n", status);
        return status;
    }
    return ZX_OK;
}

static zx_status_t aml_scpi_get_dvfs_info(void* ctx, uint8_t power_domain, scpi_opp_t* opps) {
    aml_scpi_t* scpi = ctx;
    zx_status_t status;
    struct {
        uint32_t                status;
        uint8_t                 reserved;
        uint8_t                 operating_points;
        uint16_t                latency;
        scpi_opp_entry_t        opp[MAX_DVFS_OPPS];
    } __PACKED aml_dvfs_info;

    if (!opps || power_domain >= MAX_DVFS_DOMAINS) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&scpi->lock);

    // dvfs info already populated
    if (scpi_opp[power_domain]) {
        memcpy(opps, scpi_opp[power_domain], sizeof(scpi_opp_t));
        mtx_unlock(&scpi->lock);
        return ZX_OK;
    }

    status = aml_scpi_execute_cmd(scpi,
                                              &aml_dvfs_info, sizeof(aml_dvfs_info),
                                              &power_domain, sizeof(power_domain),
                                              SCPI_CMD_GET_DVFS_INFO, SCPI_CL_DVFS);
    if (status != ZX_OK) {
        goto fail;
    }

    opps->count         = aml_dvfs_info.operating_points;
    opps->latency       = aml_dvfs_info.latency;

    if (opps->count > MAX_DVFS_OPPS) {
        SCPI_ERROR("Number of operating_points greater than MAX_DVFS_OPPS\n");
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    zxlogf(INFO, "Cluster %u details\n", power_domain);
    zxlogf(INFO, "Number of operating_points %u\n", aml_dvfs_info.operating_points);
    zxlogf(INFO, "latency %u uS\n", aml_dvfs_info.latency);

    for (uint32_t i=0; i<opps->count; i++) {
        opps->opp[i].freq_hz = aml_dvfs_info.opp[i].freq_hz;
        opps->opp[i].volt_mv = aml_dvfs_info.opp[i].volt_mv;
        zxlogf(INFO, "Operating point %d - ", i);
        zxlogf(INFO, "Freq %.4f Ghz ", (opps->opp[i].freq_hz)/(double)1000000000);
        zxlogf(INFO, "Voltage %.4f V\n", (opps->opp[i].volt_mv)/(double)1000);
    }

    scpi_opp[power_domain] = calloc(1, sizeof(scpi_opp_t));
    if (!scpi_opp[power_domain]) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    memcpy(scpi_opp[power_domain], opps, sizeof(scpi_opp_t));
    status = ZX_OK;

fail:
    mtx_unlock(&scpi->lock);
    return status;
}

static zx_status_t aml_scpi_get_dvfs_idx(void *ctx, uint8_t power_domain, uint16_t* idx) {
    aml_scpi_t* scpi = ctx;
    struct {
        uint32_t status;
        uint8_t idx;
    } __PACKED aml_dvfs_idx_info;

    if (!idx || power_domain >= MAX_DVFS_DOMAINS) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = aml_scpi_execute_cmd(scpi,
                                              &aml_dvfs_idx_info, sizeof(aml_dvfs_idx_info),
                                              &power_domain, sizeof(power_domain),
                                              SCPI_CMD_GET_DVFS, SCPI_CL_DVFS);
    if (status != ZX_OK) {
        return status;
    }

    *idx = aml_dvfs_idx_info.idx;

    SCPI_ERROR("Current Operation point %x\n", aml_dvfs_idx_info.idx);
    return ZX_OK;
}

static zx_status_t aml_scpi_set_dvfs_idx(void *ctx, uint8_t power_domain, uint16_t idx) {
    aml_scpi_t* scpi = ctx;
    struct {
        uint8_t power_domain;
        uint16_t idx;
    } __PACKED aml_dvfs_idx_info;

    if (power_domain >= MAX_DVFS_DOMAINS) {
        return ZX_ERR_INVALID_ARGS;
    }

    aml_dvfs_idx_info.power_domain  = power_domain;
    aml_dvfs_idx_info.idx           = idx;

    SCPI_INFO("OPP index for cluster %d to %d\n", power_domain, idx);
    return aml_scpi_execute_cmd(scpi,
                                NULL, 0,
                                &aml_dvfs_idx_info, sizeof(aml_dvfs_idx_info),
                                SCPI_CMD_SET_DVFS, SCPI_CL_DVFS);
}

static zx_status_t aml_scpi_get_sensor_value(void* ctx, uint32_t sensor_id,
                                      uint32_t* sensor_value) {
    aml_scpi_t* scpi = ctx;
    struct {
        uint32_t status;
        uint16_t sensor_value;
    } __PACKED aml_sensor_val;

    if (!sensor_value) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = aml_scpi_execute_cmd(scpi,
                                              &aml_sensor_val, sizeof(aml_sensor_val),
                                              &sensor_id, sizeof(sensor_id),
                                              SCPI_CMD_SENSOR_VALUE, SCPI_CL_THERMAL);
    if (status != ZX_OK) {
        return status;
    }
    *sensor_value = aml_sensor_val.sensor_value;
    return ZX_OK;
}

static zx_status_t aml_scpi_get_sensor(void* ctx, const char* name,
                                uint32_t* sensor_value) {
    aml_scpi_t* scpi = ctx;
    struct {
        uint32_t status;
        uint16_t num_sensors;
    } __PACKED aml_sensor_cap;
    struct {
        uint32_t status;
        uint16_t sensor;
        uint8_t class;
        uint8_t trigger;
        char sensor_name[20];
    } __PACKED aml_sensor_info;

    if (sensor_value == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    // First let's find information about all sensors
    zx_status_t status = aml_scpi_execute_cmd(scpi,
                                              &aml_sensor_cap, sizeof(aml_sensor_cap),
                                              NULL, 0,
                                              SCPI_CMD_SENSOR_CAPABILITIES, SCPI_CL_THERMAL);
    if (status != ZX_OK) {
        return status;
    }

    // Loop through all the sensors
    for (uint32_t sensor_id=0; sensor_id<aml_sensor_cap.num_sensors; sensor_id++) {

        status = aml_scpi_execute_cmd(scpi,
                                      &aml_sensor_info, sizeof(aml_sensor_info),
                                      &sensor_id, sizeof(sensor_id),
                                      SCPI_CMD_SENSOR_INFO, SCPI_CL_THERMAL);
        if (status != ZX_OK) {
            return status;
        }
        if (!strncmp(name, aml_sensor_info.sensor_name, sizeof(aml_sensor_info.sensor_name))) {
            *sensor_value = sensor_id;
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
    .get_dvfs_info      = aml_scpi_get_dvfs_info,
    .get_dvfs_idx       = aml_scpi_get_dvfs_idx,
    .set_dvfs_idx       = aml_scpi_set_dvfs_idx,
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

    mtx_init(&scpi->lock, mtx_plain);
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
