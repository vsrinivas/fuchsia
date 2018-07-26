// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hw/reg.h>
#include <lib/sync/completion.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/mailbox.h>
#include <ddk/debug.h>

#define SCPI_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SCPI_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define CMD_ID_SHIFT            0
#define CMD_ID_MASK             0xff
#define CMD_SENDER_ID_SHIFT     8
#define CMD_SENDER_ID_MASK      0xff
#define CMD_DATA_SIZE_SHIFT     20
#define CMD_DATA_SIZE_MASK      0x1ff
#define PACK_SCPI_CMD(cmd, sender, txsz)               \
                                    ((((cmd) & CMD_ID_MASK) << CMD_ID_SHIFT) |          \
                                    (((sender) & CMD_SENDER_ID_MASK) << CMD_SENDER_ID_SHIFT) |  \
                                    (((txsz) & CMD_DATA_SIZE_MASK) << CMD_DATA_SIZE_SHIFT))


typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    mailbox_protocol_t                  mailbox;
    scpi_protocol_t                     scpi;
    mtx_t                               lock;
} aml_scpi_t;

enum aml_scpi_client_id {
    SCPI_CL_NONE,
    SCPI_CL_CLOCKS,
    SCPI_CL_DVFS,
    SCPI_CL_POWER,
    SCPI_CL_THERMAL,
    SCPI_CL_REMOTE,
    SCPI_CL_LED_TIMER,
    SCPI_MAX,
 };

enum aml_scpi_cmd {
    SCPI_CMD_INVALID                = 0x00,
    SCPI_CMD_SCPI_READY             = 0x01,
    SCPI_CMD_SCPI_CAPABILITIES      = 0x02,
    SCPI_CMD_EVENT                  = 0x03,
    SCPI_CMD_SET_CSS_PWR_STATE      = 0x04,
    SCPI_CMD_GET_CSS_PWR_STATE      = 0x05,
    SCPI_CMD_CFG_PWR_STATE_STAT     = 0x06,
    SCPI_CMD_GET_PWR_STATE_STAT     = 0x07,
    SCPI_CMD_SYS_PWR_STATE          = 0x08,
    SCPI_CMD_L2_READY               = 0x09,
    SCPI_CMD_SET_AP_TIMER           = 0x0a,
    SCPI_CMD_CANCEL_AP_TIME         = 0x0b,
    SCPI_CMD_DVFS_CAPABILITIES      = 0x0c,
    SCPI_CMD_GET_DVFS_INFO          = 0x0d,
    SCPI_CMD_SET_DVFS               = 0x0e,
    SCPI_CMD_GET_DVFS               = 0x0f,
    SCPI_CMD_GET_DVFS_STAT          = 0x10,
    SCPI_CMD_SET_RTC                = 0x11,
    SCPI_CMD_GET_RTC                = 0x12,
    SCPI_CMD_CLOCK_CAPABILITIES     = 0x13,
    SCPI_CMD_SET_CLOCK_INDEX        = 0x14,
    SCPI_CMD_SET_CLOCK_VALUE        = 0x15,
    SCPI_CMD_GET_CLOCK_VALUE        = 0x16,
    SCPI_CMD_PSU_CAPABILITIES       = 0x17,
    SCPI_CMD_SET_PSU                = 0x18,
    SCPI_CMD_GET_PSU                = 0x19,
    SCPI_CMD_SENSOR_CAPABILITIES    = 0x1a,
    SCPI_CMD_SENSOR_INFO            = 0x1b,
    SCPI_CMD_SENSOR_VALUE           = 0x1c,
    SCPI_CMD_SENSOR_CFG_PERIODIC    = 0x1d,
    SCPI_CMD_SENSOR_CFG_BOUNDS      = 0x1e,
    SCPI_CMD_SENSOR_ASYNC_VALUE     = 0x1f,
    SCPI_CMD_SET_USR_DATA           = 0x20,
    SCPI_CMD_MAX                    = 0x21,
 };

#define VALID_CMD(cmd)           (cmd>SCPI_CMD_INVALID && cmd<SCPI_CMD_MAX)

static uint32_t aml_high_priority_cmds[] = {
    SCPI_CMD_GET_DVFS,
    SCPI_CMD_SET_DVFS,
    SCPI_CMD_SET_CLOCK_VALUE,
};

static uint32_t aml_low_priority_cmds[] = {
    SCPI_CMD_GET_DVFS_INFO,
    SCPI_CMD_SENSOR_CAPABILITIES,
    SCPI_CMD_SENSOR_INFO,
    SCPI_CMD_SENSOR_VALUE,
 };

static uint32_t aml_secure_cmds[] = {
    SCPI_CMD_SET_CSS_PWR_STATE,
    SCPI_CMD_SYS_PWR_STATE,
};
