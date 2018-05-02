// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#define FANCTL_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define FANCTL_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define TRIGGER_LEVEL_0 50
#define TRIGGER_LEVEL_1 60
#define TRIGGER_LEVEL_2 70

// MMIO Indexes
enum {
    MMIO_MAILBOX,
    MMIO_MAILBOX_PAYLOAD,
};

// GPIO Indexes
enum {
    FAN_CTL0,
    FAN_CTL1,
};

typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;

    gpio_protocol_t                     gpio;

    thrd_t                              main_thread;

    io_buffer_t                         mmio_mailbox;
    io_buffer_t                         mmio_mailbox_payload;

    zx_handle_t                         inth;
} aml_fanctl_t;

zx_status_t aml_get_sensor(aml_fanctl_t *fanctl, const char *name, uint32_t *sensor_value);
zx_status_t aml_get_sensor_value(aml_fanctl_t *fanctl, uint32_t sensor_id, uint32_t *sensor_value);
