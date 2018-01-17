// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>

typedef enum {
    AML_I2C_A,
    AML_I2C_B,
    AML_I2C_C,
    AML_I2C_D,
    AML_I2C_COUNT,
} aml_i2c_port_t;

typedef struct {
    aml_i2c_port_t port;
    zx_paddr_t     base_phys;
    uint32_t       irqnum;
} aml_i2c_dev_desc_t;

typedef struct {
    i2c_protocol_t proto;
    struct aml_i2c_dev* i2c_devs[AML_I2C_COUNT];
} aml_i2c_t;

zx_status_t aml_i2c_init(aml_i2c_t* i2c, aml_i2c_dev_desc_t* i2c_devs, size_t i2c_dev_count);
