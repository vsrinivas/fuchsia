// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/scpi.h>
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

// GPIO Indexes
enum {
    FAN_CTL0,
    FAN_CTL1,
};

typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;

    gpio_protocol_t                     gpio;
    scpi_protocol_t                     scpi;

    thrd_t                              main_thread;
} aml_fanctl_t;
