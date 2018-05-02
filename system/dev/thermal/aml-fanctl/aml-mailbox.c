// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include "aml-fanctl.h"
#include "aml-mailbox.h"

/* For the thermal use-case we need to use the
   low priority non-secure mailbox
   Mailbox 1: SCP to AP
   Mailbox 4: AP to SCP
*/

zx_status_t aml_mailbox_send_cmd(aml_fanctl_t *fanctl, aml_mhu_data_buf_t* mdata) {

    aml_mailbox_block_t *tx_mailbox = &vim2_mailbox_block[mdata->tx_mailbox];
    aml_mailbox_block_t *rx_mailbox = &vim2_mailbox_block[mdata->rx_mailbox];

    if (mdata->rx_size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

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

    zx_status_t status = zx_interrupt_wait(fanctl->inth, NULL);
    if (status != ZX_OK) {
        FANCTL_ERROR("zx_interrupt_wait failed\n");
        return status;
    }

    // AP reads the Payload to get requested information
    uint32_t num = GET_NUM_WORDS(mdata->rx_size);
    uint32_t *rx_payload = (uint32_t*)(mdata->rx_buf);
    for (uint32_t i=0; i<num; i++) {
        rx_payload[i] = READ32_MAILBOX_PL_REG(rx_mailbox->payload_offset + i);
    }

    // AP writes to the Mailbox CLR register
    WRITE32_MAILBOX_REG(rx_mailbox->clr_offset, 1);
    return ZX_OK;
}

zx_status_t aml_get_sensor_value(aml_fanctl_t *fanctl, uint32_t sensor_id, uint32_t *sensor_value) {
    aml_mhu_data_buf_t mdata = {};
    struct {
        uint32_t status;
        uint16_t sensor_value;
    } __PACKED aml_sensor_val;

    mdata.cmd             = SCP_CMD_SENSOR_VALUE;
    mdata.rx_buf          = (void *)&aml_sensor_val;
    mdata.rx_size         = sizeof(aml_sensor_val);
    mdata.rx_mailbox      = 1;
    mdata.tx_mailbox      = 4;
    mdata.tx_buf          = (void*)&sensor_id;
    mdata.tx_size         = sizeof(sensor_id);

    zx_status_t status = aml_mailbox_send_cmd(fanctl, &mdata);
    if (status != ZX_OK || aml_sensor_val.status != 0) {
        FANCTL_ERROR("aml_mailbox_send_cmd failed\n");
        return status;
    }

    *sensor_value = aml_sensor_val.sensor_value;
    return ZX_OK;
}

zx_status_t aml_get_sensor(aml_fanctl_t *fanctl,
                           const char *name, uint32_t *sensor_value) {
    aml_mhu_data_buf_t mdata = {};
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

    mdata.cmd          = SCP_CMD_SENSOR_CAPABILITIES;
    mdata.rx_buf       = (void *)&aml_sensor_cap;
    mdata.rx_size      = sizeof(aml_sensor_cap);
    mdata.rx_mailbox   = 1;
    mdata.tx_mailbox   = 4;

    // First let's find how many sensors are there on the board
    zx_status_t status = aml_mailbox_send_cmd(fanctl, &mdata);
    if (status != ZX_OK || aml_sensor_cap.status != 0) {
        FANCTL_ERROR("aml_mailbox_send_cmd failed\n");
        return status;
    }

    mdata.cmd          = SCP_CMD_SENSOR_INFO;
    mdata.rx_buf       = (void *)&aml_sensor_info;
    mdata.rx_size      = sizeof(aml_sensor_info);
    mdata.rx_mailbox   = 1;
    mdata.tx_mailbox   = 4;

    // Loop through all the sensors
    for (uint16_t sensor_id=0; sensor_id<aml_sensor_cap.num_sensors; sensor_id++) {
        mdata.tx_buf    = (void*)&sensor_id;
        mdata.tx_size   = sizeof(sensor_id);
        status = aml_mailbox_send_cmd(fanctl, &mdata);
        if (status != ZX_OK || aml_sensor_info.status != 0) {
            FANCTL_ERROR("aml_mailbox_send_cmd failed\n");
            return status;
        }

        if (!strcmp(name, aml_sensor_info.sensor_name)) {
            *sensor_value = aml_sensor_info.sensor;
            break;
        }
    }
    return ZX_OK;
}
